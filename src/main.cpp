#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/epoll.h>
#include <csignal>
#include <atomic>
#include <cerrno>
#include <cstring>

#include "bt.h"
#include "usb.h"
#include "audio.h"
#include "utils.h"

#define MAX_EVENTS 10

std::atomic<bool> daemon_running{true};
int reportSeqCounter = 0;

void signal_handler(int signum) {
    printf("\nCaught signal %d. Initiating graceful shutdown...\n", signum);
    daemon_running = false;
}

// Callback when Bluetooth receives data
void on_bt_data(uint8_t *data, uint16_t len) {
    // Basic L2CAP filtering for Phase 4
    // We expect ACL Data (Packet Type 0x02)
    // Then L2CAP header, then HID payload.
    // Assuming data[0] is packet_type, data[1..] is HCI/ACL data
    if (len > 9 && data[0] == 0x02) {
        // Assume basic routing logic from old code for now:
        // ACL header (4), L2CAP header (4), PSM specific payload.
        // If it's a standard DualSense report (0x31)
        if (data[9] == 0x31 && ep1_fd >= 0) {
            // Write 63 bytes of report data to ep1 (USB IN)
            write(ep1_fd, data + 11, 63);
        }
    }
}

int main() {
    printf("Starting Pico2W DualSense Bridge (Linux Daemon Phase 4)\n");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int bt_fd = bt_init();
    if (bt_fd == -1) {
        printf("Failed to initialize Bluetooth. Exiting.\n");
        return 1;
    }

    bt_register_data_callback(on_bt_data);

    audio_init();

    if (usb_init() < 0) {
        printf("Failed to initialize USB FunctionFS. Exiting.\n");
        return 1;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return 1;
    }

    struct epoll_event ev;

    // Add BT socket to epoll
    if (bt_fd != -1) {
        ev.events = EPOLLIN;
        ev.data.fd = bt_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, bt_fd, &ev) == -1) {
            perror("epoll_ctl: bt_fd");
            return 1;
        }
    }

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
    if (ep2_fd != -1) {
        ev.events = EPOLLIN;
        ev.data.fd = ep2_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ep2_fd, &ev) == -1) {
            perror("epoll_ctl: ep2_fd");
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

            if (fd == bt_fd && (events[n].events & EPOLLIN)) {
                bt_handle_data();
            }
            else if (fd == ep0_fd && (events[n].events & EPOLLIN)) {
                usb_handle_ep0();
            }
            else if (fd == ep2_fd && (events[n].events & EPOLLIN)) {
                // USB OUT endpoint received data (from Steam/Proton)
                uint8_t buf[64];
                int ret = read(ep2_fd, buf, sizeof(buf));
                if (ret > 0) {
                    // Check if it's a SET_REPORT for rumble/triggers (Report ID 0x02)
                    if (buf[0] == 0x02) {
                        uint8_t outputData[78];
                        memset(outputData, 0, sizeof(outputData));
                        outputData[0] = 0x31;
                        outputData[1] = reportSeqCounter << 4;
                        if (++reportSeqCounter == 256) {
                            reportSeqCounter = 0;
                        }
                        outputData[2] = 0x10;
                        memcpy(outputData + 3, buf + 1, ret - 1);

                        // Send over BT. 0x02 is HCI ACL data packet type.
                        bt_write(0x02, outputData, sizeof(outputData));
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
