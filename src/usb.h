#ifndef DS5_BRIDGE_USB_H
#define DS5_BRIDGE_USB_H

#include <cstdint>
#include <atomic>

extern uint8_t mute[2]; // 0: SPEAKER(0x02) 1: MIC(0x05)
extern float volume[2]; // 0: SPEAKER(0x02) 1: MIC(0x05)

extern int ep0_fd;
extern int ep_hid_in_fd;
extern int ep_hid_out_fd;
extern bool usb_gadget_bound;
extern bool uac1_enabled;
extern std::atomic<bool> usb_shutting_down;

int usb_init();
void usb_deinit();
void usb_unbind_udc();
void usb_close_fds();
void usb_handle_ep0();
void usb_process_pending_feature(uint8_t report_id);

// Thread-safe input report queue — enqueue from any thread,
// a dedicated writer thread drains the queue with blocking writes
// to eliminate the FunctionFS O_NONBLOCK race condition.
void usb_enqueue_input_report(const uint8_t *data, size_t len);
void usb_start_in_thread();
void usb_stop_in_thread();

#endif //DS5_BRIDGE_USB_H
