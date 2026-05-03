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
        // USB output report (ID 0x02) from host → translate to BT output report (0x31)
        //
        // For hidraw write, we send: [0x31] [seq] [tag] [data...]
        // No 0xA2 prefix — bt_write() handles stripping it if present,
        // but we build it in the old format for compatibility with audio.cpp
        // which also calls bt_write(INTERRUPT, ...) with the 0xA2 prefix.
        uint8_t outputData[78];
        memset(outputData, 0, sizeof(outputData));

        outputData[0] = 0xA2; // HID DATA header (bt_write strips this)
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

// Callback when Bluetooth/hidraw receives data
void on_bt_data(CHANNEL_TYPE channel, uint8_t *data, uint16_t len) {
    if (channel == CONTROL) {
        // Feature report responses — now handled via ioctl in usb.cpp
        // This path is kept for compatibility but shouldn't be called
        // in the hidraw architecture.
        if (len > 1 && data[0] == 0xA3) {
            uint8_t report_id = data[1];
            feature_data[report_id].assign(data + 1, data + len);
            usb_process_pending_feature(report_id);
        }
        return;
    }

    // Ignore control channel for now in main mapping
    if (channel != INTERRUPT) return;

    if (len > 1 && data[0] == 0xA1) { // HID DATA header (added by bt.cpp for compatibility)
        if (data[1] == 0x31 && ep_hid_in_fd >= 0) { // 0x31 is DualSense BT State report
            // We need to map 0x31 (BT) -> 0x01 (USB)
            // BT 0x31: [A1] [31] [seq] [buttons/sticks...]
            // USB 0x01: [01] [buttons/sticks...] (64 bytes total)

            uint8_t usb_report[64];
            memset(usb_report, 0, sizeof(usb_report));

            if (len >= 65) {
                // Skip A1, 31, seq (data[0], data[1], data[2])
                // Copy 63 bytes (buttons/sticks/sensors)

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
    printf("Modo: hidraw → USB Gadget (FunctionFS)\n\n");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return 1;
    }

    // ================================================================
    // Phase 1: Detect and open DualSense hidraw device
    // ================================================================
    // The DualSense must already be connected via Bluetooth (managed by
    // bluetoothd). We simply find and open its /dev/hidraw* node.
    // No modules are unloaded, no services are stopped.

    printf("[Main] 📡 Aguardando DualSense conectado via Bluetooth...\n");
    printf("[Main] 👉 Conecte o controle normalmente (bluetoothctl ou GUI).\n\n");

    // Retry finding the hidraw device — the user may connect after starting
    while (daemon_running) {
        if (bt_init(epoll_fd) == 0) {
            break;
        }
        printf("[Main] Tentando novamente em 3 segundos...\n\n");
        sleep(3);
    }

    if (!daemon_running || !bt_is_connected()) {
        printf("[Main] Abortado. Encerrando.\n");
        close(epoll_fd);
        return 1;
    }

    bt_register_data_callback(on_bt_data);
    printf("[Main] ✅ DualSense conectado via hidraw!\n\n");

    // ================================================================
    // Phase 2: Initialize USB gadget (HID via FunctionFS)
    // ================================================================
    // hid_playstation is already loaded (it's what created the hidraw
    // node in the first place), so no need to reload it.

    if (usb_init() < 0) {
        printf("Failed to initialize USB FunctionFS. Exiting.\n");
        bt_deinit();
        close(epoll_fd);
        return 1;
    }

    // Initialize audio (ALSA capture from f_uac1 + Opus encoding)
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

    struct epoll_event events[MAX_EVENTS];

    printf("Entering main event loop...\n");
    while (daemon_running) {
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
                // Check if it's the hidraw — controller may have disconnected
                if (bt_is_fd_mine(events[n].data.fd)) {
                    printf("[Main] ⚠️ Hidraw error/hangup — controle desconectou.\n");
                    bt_process_epoll_event(events[n].data.fd);
                    if (!bt_is_connected()) {
                        printf("[Main] 🔄 Controle perdido. Tentando reconectar...\n");
                        // TODO: implement reconnection loop
                        daemon_running = false;
                    }
                    continue;
                }
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

        // Manual polling fallback for HID OUT
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
