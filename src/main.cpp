#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/epoll.h>
#include <csignal>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <map>
#include <vector>

#include "bt.h"
#include "usb.h"
#include "audio.h"
#include "utils.h"

#define MAX_EVENTS 10

std::map<uint8_t, std::vector<uint8_t>> feature_data;
std::atomic<bool> daemon_running{true};
int reportSeqCounter = 0;

void signal_handler(int signum) {
    printf("\nCaught signal %d. Initiating graceful shutdown...\n", signum);
    daemon_running = false;
}

// Callback when Bluetooth receives data
void on_bt_data(CHANNEL_TYPE channel, uint8_t *data, uint16_t len) {
    if (channel == CONTROL) {
        if (len > 1 && data[0] == 0xA3) {
            uint8_t report_id = data[1];
            feature_data[report_id].assign(data + 1, data + len);
            usb_process_pending_feature(report_id);
        }
        return;
    }

    // Ignore control channel for now in main mapping
    if (channel != INTERRUPT) return;

    if (len > 1 && data[0] == 0xA1) { // HID DATA header
        if (data[1] == 0x31 && ep_hid_in_fd >= 0) { // 0x31 is DualSense State report
            // We need to map 0x31 (BT) -> 0x01 (USB)
            // BT 0x31: [A1] [31] [seq] [buttons/sticks...]
            // USB 0x01: [01] [buttons/sticks...] (64 bytes total, we write 63 payload)

            uint8_t usb_report[64];
            memset(usb_report, 0, sizeof(usb_report));

            if (len >= 65) {
                // Skip A1, 31, seq (data[0], data[1], data[2])
                // Copy 63 bytes (buttons/sticks/sensors)
                memcpy(usb_report, data + 3, 63); // Notice we don't include Report ID in the ffs write directly

                // Write 63 bytes of report data to ep1 (USB IN)
                // wait, the USB gadget expects the payload only, no report ID if using ffs IN endpoint like this?
                // Actually, the descriptor has Report ID 0x01. So the first byte must be the report ID!

                uint8_t final_usb_report[64];
                final_usb_report[0] = 0x01;
                memcpy(final_usb_report + 1, data + 3, 63);

                write(ep_hid_in_fd, final_usb_report, 64);
            }
        }
    }
}

int main() {
    printf("Starting Pico2W DualSense Bridge (Linux Daemon Phase 4)\n");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return 1;
    }

    if (bt_init(epoll_fd) == -1) {
        printf("Failed to initialize Bluetooth. Exiting.\n");
        return 1;
    }

    bt_register_data_callback(on_bt_data);

    audio_init();

    if (usb_init() < 0) {
        printf("Failed to initialize USB FunctionFS. Exiting.\n");
        return 1;
    }

    struct epoll_event ev;

    // Add USB EP0 to epoll
    if (ep0_fd != -1) {
        ev.events = EPOLLIN;
        ev.data.fd = ep0_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ep0_fd, &ev) == -1) {
            perror("epoll_ctl: ep0_fd");
            return 1;
        }
    }

    // Add USB EP2 (OUT) to epoll
    if (ep_hid_out_fd != -1) {
        ev.events = EPOLLIN;
        ev.data.fd = ep_hid_out_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ep_hid_out_fd, &ev) == -1) {
            perror("epoll_ctl: ep_hid_out_fd");
            return 1;
        }
    }

    // Add USB EP1 (Audio OUT) to epoll
    if (ep_audio_out_fd != -1) {
        ev.events = EPOLLIN;
        ev.data.fd = ep_audio_out_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ep_audio_out_fd, &ev) == -1) {
            perror("epoll_ctl: ep_audio_out_fd");
            return 1;
        }
    }

    struct epoll_event events[MAX_EVENTS];

    printf("Entering main event loop...\n");
    while (daemon_running) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].events & (EPOLLERR | EPOLLHUP)) {
                // Ignore for now
            }

            int fd = events[n].data.fd;

            if (bt_is_fd_mine(fd) && (events[n].events & EPOLLIN)) {
                bt_process_epoll_event(fd);
            }
            else if (fd == ep0_fd && (events[n].events & EPOLLIN)) {
                usb_handle_ep0();
            }
            else if (fd == ep_audio_out_fd && (events[n].events & EPOLLIN)) {
                int16_t buf[196];
                while (true) {
                    int ret = read(ep_audio_out_fd, buf, sizeof(buf));
                    if (ret > 0) {
                        audio_receive_pcm(buf, ret);
                    } else if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    } else {
                        break;
                    }
                }
            }
            else if (fd == ep_hid_out_fd && (events[n].events & EPOLLIN)) {
                // USB OUT endpoint received data (from Steam/Proton)
                uint8_t buf[64];
                int ret = read(ep_hid_out_fd, buf, sizeof(buf));
                if (ret > 0) {
                    // Check if it's a SET_REPORT for rumble/triggers (Report ID 0x02)
                    if (buf[0] == 0x02) {
                        uint8_t outputData[78];
                        memset(outputData, 0, sizeof(outputData));

                        outputData[0] = 0xA2; // HID DATA
                        outputData[1] = 0x31; // DualSense Output Report ID
                        outputData[2] = reportSeqCounter << 4;
                        if (++reportSeqCounter == 16) {
                            reportSeqCounter = 0;
                        }
                        outputData[3] = 0x10; // Flags? Usually 0x10 or 0x00
                        // The `ret` from USB EP2 should be 64 bytes (1 byte ID `0x02` + 63 bytes payload).
                        // We copy `ret - 1` (63) bytes to `outputData + 4`.
                        // Then we calculate CRC for the remaining 4 bytes, so total size 1+1+1+1+63+4 = 71 bytes?
                        // Wait, old_main.cpp used `sizeof(outputData)` which is 78 bytes.
                        // The 78 bytes total length is: 0xA2, 0x31, seq, 0x10, then 70 bytes of payload (which includes padding) and 4 CRC.
                        // Let's copy the payload and calculate the CRC on the whole 77 bytes (index 1 to 77).
                        memcpy(outputData + 4, buf + 1, ret - 1);

                        fill_output_report_checksum(outputData + 1, sizeof(outputData) - 1);

                        bt_write(INTERRUPT, outputData, sizeof(outputData));
                    }
                }
            }
        }
    }

    printf("Cleaning up...\n");
    audio_deinit();
    usb_deinit();
    bt_deinit();
    close(epoll_fd);
    printf("Daemon shut down successfully.\n");
    return 0;
}
