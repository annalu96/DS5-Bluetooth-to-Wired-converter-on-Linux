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

// Process HID OUT data from host (rumble, LEDs, etc.)
static void process_hid_out() {
    if (ep_hid_out_fd < 0) return;
    uint8_t buf[64];
    int ret = read(ep_hid_out_fd, buf, sizeof(buf));
    if (ret > 0 && buf[0] == 0x02) {
        uint8_t outputData[78];
        memset(outputData, 0, sizeof(outputData));

        outputData[0] = 0xA2; // HID DATA
        outputData[1] = 0x31; // DualSense Output Report ID
        outputData[2] = reportSeqCounter << 4;
        if (++reportSeqCounter == 16) {
            reportSeqCounter = 0;
        }
        outputData[3] = 0x10;
        memcpy(outputData + 4, buf + 1, ret - 1);

        fill_output_report_checksum(outputData + 1, sizeof(outputData) - 1);
        bt_write(INTERRUPT, outputData, sizeof(outputData));
    }
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
    printf("Starting DS5-Bluetooth-to-Wired-converter-on-Linux...\n");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return 1;
    }

    // ================================================================
    // Phase 1: Initialize Bluetooth and wait for DualSense connection
    // ================================================================
    // bt_init() unloads hidp + hid_playstation to prevent the kernel
    // from intercepting BT HID connections on PSM 17/19.
    // We must wait for the DualSense to connect via BT BEFORE setting
    // up the USB gadget, because hid_playstation needs to be reloaded
    // for the gadget to be recognized as a DualSense.
    if (bt_init(epoll_fd) == -1) {
        printf("Failed to initialize Bluetooth. Exiting.\n");
        return 1;
    }

    bt_register_data_callback(on_bt_data);

    printf("[Main] Aguardando reconexão do DualSense via L2CAP...\n");

    // Block until both L2CAP channels (control + interrupt) are connected
    struct epoll_event wait_events[MAX_EVENTS];
    while (daemon_running && !bt_is_connected()) {
        int nfds = epoll_wait(epoll_fd, wait_events, MAX_EVENTS, 1000);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait (BT wait)");
            break;
        }
        for (int n = 0; n < nfds; ++n) {
            if (bt_is_fd_mine(wait_events[n].data.fd) && (wait_events[n].events & EPOLLIN)) {
                bt_process_epoll_event(wait_events[n].data.fd);
            }
        }
    }

    if (!daemon_running || !bt_is_connected()) {
        printf("[Main] Aborted or BT connection failed. Cleaning up.\n");
        bt_deinit();
        close(epoll_fd);
        return 1;
    }

    printf("[Main] DualSense connected via Bluetooth!\n");

    // ================================================================
    // Phase 2: Reload hid_playstation for USB gadget recognition
    // ================================================================
    // The USB gadget uses Sony VID/PID (054c:0ce6), so hid_playstation
    // must be loaded for the host to recognize it as a DualSense.
    // We keep hidp UNLOADED to prevent BT HID interception.
    bt_reload_hid_playstation();

    // ================================================================
    // Phase 3: Initialize USB gadget (HID via FunctionFS)
    // ================================================================
    if (usb_init() < 0) {
        printf("Failed to initialize USB FunctionFS. Exiting.\n");
        bt_deinit();
        close(epoll_fd);
        return 1;
    }

    // Initialize audio (ALSA capture from f_uac1 + Opus encoding)
    // Only available when UAC1 is enabled (requires UDC with isochronous support)
    if (uac1_enabled) {
        audio_init();
    } else {
        printf("[Audio] Skipped — UAC1 not available on this UDC.\n");
    }

    struct epoll_event ev;

    // Add USB EP0 to epoll (for HID control requests)
    if (ep0_fd != -1) {
        ev.events = EPOLLIN;
        ev.data.fd = ep0_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ep0_fd, &ev) == -1) {
            perror("epoll_ctl: ep0_fd");
            return 1;
        }
    }

    // Try to add USB HID OUT endpoint to epoll.
    // FunctionFS endpoint files may not support epoll on some kernels,
    // in which case we fall back to manual non-blocking reads.
    bool hid_out_in_epoll = false;
    if (ep_hid_out_fd != -1) {
        ev.events = EPOLLIN;
        ev.data.fd = ep_hid_out_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ep_hid_out_fd, &ev) == -1) {
            printf("[USB] ep_hid_out_fd does not support epoll (errno=%d: %s), using manual polling.\n",
                   errno, strerror(errno));
        } else {
            hid_out_in_epoll = true;
        }
    }

    // NOTE: Audio is no longer monitored via epoll here.
    // The ALSA capture is handled in its own thread in audio.cpp.

    struct epoll_event events[MAX_EVENTS];

    printf("Entering main event loop...\n");
    while (daemon_running) {
        // Use a short timeout so we can poll ep_hid_out_fd if it's not in epoll
        int timeout_ms = hid_out_in_epoll ? -1 : 4; // 4ms ≈ HID polling interval
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout_ms);
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
            else if (fd == ep_hid_out_fd && (events[n].events & EPOLLIN)) {
                process_hid_out();
            }
        }

        // Manual polling fallback: try non-blocking read if epoll doesn't support it
        if (!hid_out_in_epoll) {
            process_hid_out();
        }
    }

    printf("Cleaning up...\n");
    if (uac1_enabled) {
        audio_deinit();
    }
    usb_deinit();
    bt_deinit();
    close(epoll_fd);
    printf("Daemon shut down successfully.\n");
    return 0;
}
