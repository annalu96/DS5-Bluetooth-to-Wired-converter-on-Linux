#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/epoll.h>
#include <poll.h>
#include <csignal>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <map>
#include <vector>
#include <chrono>
#include <thread>

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

// HID OUT reader thread — runs in a dedicated thread because FunctionFS
// endpoint read() is inherently blocking (ignores O_NONBLOCK and poll()).
// This thread blocks on read() waiting for host output reports (rumble, LEDs)
// while the main thread runs the epoll loop for BT→USB data flow.
static void hid_out_thread_func() {
    printf("[HID-OUT] Thread started for host→controller output reports.\n");
    while (daemon_running && ep_hid_out_fd >= 0) {
        uint8_t buf[64];
        int ret = read(ep_hid_out_fd, buf, sizeof(buf));
        if (ret <= 0) {
            if (ret == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                printf("[HID-OUT] Endpoint closed or error: %s\n", strerror(errno));
                break;
            }
            continue;
        }
        if (buf[0] == 0x02) {
            // USB output report (ID 0x02) from host → translate to BT output report (0x31)
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

            static int out_count = 0;
            if (out_count++ < 3 || out_count % 100 == 0) {
                printf("[HID-OUT] Forwarded output report #%d (%d bytes)\n", out_count, ret);
            }
        }
    }
    printf("[HID-OUT] Thread exiting.\n");
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

            if (len >= 65) {
                // Skip A1, 31, seq (data[0], data[1], data[2])
                // Copy 63 bytes (buttons/sticks/sensors)

                uint8_t final_usb_report[64];
                final_usb_report[0] = 0x01;
                memcpy(final_usb_report + 1, data + 3, 63);

                static int debug_counter = 0;
                static int sent_ok = 0;
                static int sent_eagain = 0;
                static int sent_fail = 0;

                // Log first 5 reports unconditionally for debugging,
                // then every 500th report with cumulative stats
                if (debug_counter < 5 || debug_counter % 500 == 0) {
                    printf("[USB] Forwarding input report #%d: LX=%02x LY=%02x RX=%02x RY=%02x L2=%02x R2=%02x "
                           "(ok=%d eagain=%d fail=%d)\n",
                           debug_counter,
                           final_usb_report[1], final_usb_report[2], final_usb_report[3],
                           final_usb_report[4], final_usb_report[5], final_usb_report[6],
                           sent_ok, sent_eagain, sent_fail);
                }
                debug_counter++;

                ssize_t wret = write(ep_hid_in_fd, final_usb_report, 64);
                if (wret < 0 && errno != EAGAIN) {
                    sent_fail++;
                    if (sent_fail <= 5 || sent_fail % 100 == 0) {
                        printf("[USB] ❌ Failed to write to ep_hid_in_fd: %s (fail #%d)\n",
                               strerror(errno), sent_fail);
                    }
                } else if (wret < 0 && errno == EAGAIN) {
                    sent_eagain++;
                } else {
                    sent_ok++;
                }
            } else {
                static int small_len_count = 0;
                if (small_len_count++ % 100 == 0) {
                    printf("[USB] ⚠️ Ignoring 0x31 report due to small len=%d\n", len);
                }
            }
        } else {
            static int unhandled_count = 0;
            if (unhandled_count++ % 100 == 0) {
                printf("[USB] ⚠️ Ignoring unhandled report ID: 0x%02x (len=%d)\n", data[1], len);
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

    // Start HID OUT reader in a dedicated thread.
    // FunctionFS endpoint read() is inherently blocking — it ignores
    // O_NONBLOCK and poll(). A separate thread lets it block harmlessly
    // while the main loop processes BT→USB input reports.
    std::thread hid_out_thread;
    if (ep_hid_out_fd >= 0) {
        hid_out_thread = std::thread(hid_out_thread_func);
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
                // Check if it's the hidraw — controller may have disconnected
                if (bt_is_fd_mine(events[n].data.fd)) {
                    printf("[Main] ⚠️ Hidraw error/hangup — controle desconectou.\n");
                    bt_process_epoll_event(events[n].data.fd);
                    if (!bt_is_connected()) {
                        printf("[Main] 🔄 Controle perdido. Tentando reconectar...\n");
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
        }
    }

    // Wait for HID OUT thread to finish
    if (hid_out_thread.joinable()) {
        // Close the fd to unblock the read() in the thread
        if (ep_hid_out_fd >= 0) {
            close(ep_hid_out_fd);
            ep_hid_out_fd = -1;
        }
        hid_out_thread.join();
        printf("[HID-OUT] Thread joined.\n");
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
