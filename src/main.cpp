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
#include <mutex>

#include "bt.h"
#include "usb.h"
#include "audio.h"
#include "utils.h"

#define MAX_EVENTS 10

std::map<uint8_t, std::vector<uint8_t>> feature_data;
std::atomic<bool> daemon_running{true};
static int reportSeqCounter = 0;
static std::mutex seq_mutex; // Protects reportSeqCounter from concurrent access

// Send a brief rumble pulse directly to the BT controller to verify
// the downstream output path works independently of the host.
static void send_startup_rumble_test() {
    printf("[TEST] 🔧 Sending direct rumble test to BT controller...\n");

    // Build a valid BT output report: [A2][31][seq][tag][common 47][reserved 24][CRC 4]
    uint8_t outputData[79];
    memset(outputData, 0, sizeof(outputData));

    outputData[0] = 0xA2; // HID DATA header (bt_write strips this)
    outputData[1] = 0x31; // BT Output Report ID
    outputData[2] = (reportSeqCounter << 4) | 0x00; // seq_tag
    if (++reportSeqCounter == 16) reportSeqCounter = 0;
    outputData[3] = 0x10; // DS_OUTPUT_TAG (mandatory)

    // Common payload starts at outputData[4]:
    //   [4] = valid_flag0, [5] = valid_flag1
    //   [6] = motor_right, [7] = motor_left
    outputData[4] = 0x03; // valid_flag0: COMPATIBLE_VIBRATION | HAPTICS_SELECT
    outputData[5] = 0x00; // valid_flag1
    outputData[6] = 128;  // motor_right (weak)
    outputData[7] = 128;  // motor_left (strong)

    // Compute CRC32 over [0x31..reserved] (74 bytes), using 0xA2 seed
    fill_output_report_checksum(outputData + 1, sizeof(outputData) - 1);

    printf("[TEST]    Report hex (first 12): ");
    for (int i = 0; i < 12; i++) printf("%02x ", outputData[i]);
    printf("\n");

    bt_write(INTERRUPT, outputData, sizeof(outputData));
    printf("[TEST] ✅ Rumble ON sent. Waiting 800ms...\n");

    usleep(800000); // 800ms vibration

    // Send stop: motors = 0
    memset(outputData, 0, sizeof(outputData));
    outputData[0] = 0xA2;
    outputData[1] = 0x31;
    outputData[2] = (reportSeqCounter << 4) | 0x00;
    if (++reportSeqCounter == 16) reportSeqCounter = 0;
    outputData[3] = 0x10;
    outputData[4] = 0x03; // valid_flag0
    outputData[5] = 0x00;
    outputData[6] = 0;    // motor_right = 0
    outputData[7] = 0;    // motor_left = 0
    fill_output_report_checksum(outputData + 1, sizeof(outputData) - 1);
    bt_write(INTERRUPT, outputData, sizeof(outputData));
    printf("[TEST] ✅ Rumble OFF sent. Test complete.\n");
    printf("[TEST]    Did the controller vibrate? If YES → BT output path works.\n");
    printf("[TEST]    If NO → check hidraw write permissions and BT connection.\n\n");
}

// Translate a USB output report (ID 0x02, 63 bytes) to BT output report
// (ID 0x31, 78 bytes) and send it to the controller via hidraw.
// This function is called from:
//   - hid_out_thread_func (interrupt OUT endpoint)
//   - usb_handle_ep0 via SET_REPORT (control pipe)
// Returns: number of bytes forwarded, or -1 on error.
int translate_usb_output_to_bt(const uint8_t *usb_buf, int usb_len, const char *source) {
    static int fwd_count = 0;

    // USB format (63 bytes): [0x02] [common 47 bytes] [reserved 15 bytes]
    // BT  format (78 bytes): [0x31] [seq_tag] [tag=0x10] [common 47 bytes] [reserved 24 bytes] [CRC32 4 bytes]
    // We prepend 0xA2 as bt_write() strips it before sending to hidraw.
    uint8_t outputData[79]; // 0xA2 + 78 bytes BT report
    memset(outputData, 0, sizeof(outputData));

    outputData[0] = 0xA2; // HID DATA header (bt_write strips this)
    outputData[1] = 0x31; // DualSense BT Output Report ID
    {
        std::lock_guard<std::mutex> lock(seq_mutex);
        outputData[2] = (reportSeqCounter << 4) | 0x00; // seq_tag
        if (++reportSeqCounter == 16) reportSeqCounter = 0;
    }
    outputData[3] = 0x10; // tag = DS_OUTPUT_TAG (MANDATORY)

    // Copy the 47-byte common payload from USB report into BT report.
    // USB buf: [0x02][common 47 bytes][reserved 15 bytes] — common starts at buf[1]
    // BT out:  [0xA2][0x31][seq][tag][common 47 bytes][reserved 24][CRC32 4]
    size_t common_len = usb_len - 1; // skip USB report ID (0x02)
    if (common_len > 47) common_len = 47;
    memcpy(outputData + 4, usb_buf + 1, common_len);

    // Force COMPATIBLE_VIBRATION bit for Bluetooth compatibility.
    // USB wired mode uses HAPTICS_SELECT (0x02) alone, but BT mode
    // requires COMPATIBLE_VIBRATION (0x01) to be set for motors to work.
    outputData[4] |= 0x01;  // DS_OUTPUT_VALID_FLAG0_COMPATIBLE_VIBRATION

    // CRC32 covers bytes 1..74, result in bytes 75..78
    fill_output_report_checksum(outputData + 1, sizeof(outputData) - 1);

    if (usb_shutting_down) return -1;
    bt_write(INTERRUPT, outputData, sizeof(outputData));

    if (fwd_count < 10 || fwd_count % 100 == 0) {
        printf("[OUTPUT] ✅ Forwarded output report #%d via %s (%d USB bytes → 78 BT bytes) "
               "flags=0x%02x|0x%02x motor_r=%u motor_l=%u\n",
               fwd_count, source, usb_len,
               outputData[4], outputData[5],
               outputData[6], outputData[7]);
    }
    fwd_count++;
    return 78;
}

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
    printf("[HID-OUT] ep_hid_out_fd=%d — blocking on read() for host output reports...\n", ep_hid_out_fd);

    static int out_count = 0;
    int heartbeat = 0;

    while (daemon_running && ep_hid_out_fd >= 0 && !usb_shutting_down) {
        uint8_t buf[64];
        int ret = read(ep_hid_out_fd, buf, sizeof(buf));
        if (ret <= 0) {
            if (ret == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                printf("[HID-OUT] Endpoint closed or error: %s\n", strerror(errno));
                break;
            }
            // For EAGAIN (non-blocking mode): add heartbeat logging
            heartbeat++;
            if (heartbeat % 10000 == 1) {
                printf("[HID-OUT] 💓 Thread alive, still waiting for output reports (poll #%d)\n", heartbeat);
            }
            // Small sleep to avoid busy-wait if O_NONBLOCK is active
            usleep(1000); // 1ms
            continue;
        }

        // Log ALL received data for the first few reports
        if (out_count < 10) {
            printf("[HID-OUT] 📥 Received %d bytes from OUT endpoint, report_id=0x%02x\n", ret, buf[0]);
            printf("[HID-OUT]    Hex: ");
            for (int i = 0; i < ret && i < 20; i++) printf("%02x ", buf[i]);
            if (ret > 20) printf("...");
            printf("\n");
        }

        if (buf[0] == 0x02) {
            translate_usb_output_to_bt(buf, ret, "OUT-EP");
            out_count++;
        } else {
            printf("[HID-OUT] ⚠️ Unknown output report ID: 0x%02x (len=%d) — ignoring\n", buf[0], ret);
        }
    }
    printf("[HID-OUT] Thread exiting (forwarded %d reports total).\n", out_count);
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

                // Log first 5 reports unconditionally for debugging,
                // then every 500th report with cumulative stats
                if (debug_counter < 5 || debug_counter % 500 == 0) {
                    printf("[USB] Enqueuing input report #%d: LX=%02x LY=%02x RX=%02x RY=%02x L2=%02x R2=%02x\n",
                           debug_counter,
                           final_usb_report[1], final_usb_report[2], final_usb_report[3],
                           final_usb_report[4], final_usb_report[5], final_usb_report[6]);
                }
                debug_counter++;

                if (usb_shutting_down) return; // Don't enqueue during shutdown
                usb_enqueue_input_report(final_usb_report, 64);
            } else {
                static int small_len_count = 0;
                if (small_len_count++ % 100 == 0) {
                    printf("[USB] \u26a0\ufe0f Ignoring 0x31 report due to small len=%d\n", len);
                }
            }
        } else {
            static int unhandled_count = 0;
            if (unhandled_count++ % 100 == 0) {
                printf("[USB] \u26a0\ufe0f Ignoring unhandled report ID: 0x%02x (len=%d)\n", data[1], len);
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

    // ── Startup rumble test ──
    // Sends a direct rumble pulse to verify the BT→controller output path
    // works, independently of whether the host sends output reports.
    send_startup_rumble_test();

    // Start the dedicated HID IN writer thread.
    // This thread drains the input queue with blocking (synchronous)
    // writes to ep_hid_in_fd, avoiding the FunctionFS O_NONBLOCK
    // race condition that can cause kernel panics in dummy_hcd.
    usb_start_in_thread();

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

    // ── Graceful shutdown sequence ──
    // Order is critical to prevent kernel panics:
    // 1. Signal writers to stop (usb_shutting_down)
    // 2. Unbind UDC (cancels all pending URBs safely)
    // 3. Stop writer threads (they'll exit when write() returns error)
    // 4. Close FDs (now safe — no pending URBs)
    usb_shutting_down = true;

    // Unbind UDC FIRST — this cancels all pending URBs while FFS
    // structures are still alive, preventing use-after-free.
    usb_unbind_udc();

    // Stop the HID IN writer thread
    usb_stop_in_thread();

    // Wait for HID OUT thread to finish
    if (hid_out_thread.joinable()) {
        hid_out_thread.join();
        printf("[HID-OUT] Thread joined.\n");
    }

    printf("Cleaning up...\n");
    if (uac1_enabled) {
        audio_deinit();
    }
    // Close all FDs and run teardown script
    usb_close_fds();
    bt_deinit();
    close(epoll_fd);
    printf("Daemon shut down successfully.\n");
    return 0;
}
