#ifndef DS5_BRIDGE_USB_H
#define DS5_BRIDGE_USB_H

#include <cstdint>

extern uint8_t mute[2]; // 0: SPEAKER(0x02) 1: MIC(0x05)
extern float volume[2]; // 0: SPEAKER(0x02) 1: MIC(0x05)

extern int ep0_fd;
extern int ep_hid_in_fd;
extern int ep_hid_out_fd;

int usb_init();
void usb_deinit();
void usb_handle_ep0();
void usb_process_pending_feature(uint8_t report_id);

#endif //DS5_BRIDGE_USB_H
