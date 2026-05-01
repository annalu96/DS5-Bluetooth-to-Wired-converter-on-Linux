#include "usb.h"

uint8_t mute[2] = {0}; // 0: SPEAKER(0x02) 1: MIC(0x05)
float volume[2] = {1.0f, 1.0f}; // 0: SPEAKER(0x02) 1: MIC(0x05)

// USB Initialization stub
// Will be implemented in Phase 4 (ConfigFS / FunctionFS)
void usb_init() {
    // Stub
}

void usb_deinit() {
    // Stub
}
