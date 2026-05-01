#include "bt.h"
#include <cstdio>

// Bluetooth Initialization stub
// Will be implemented in Phase 3 (BlueZ RAW HCI)
void bt_init() {
    printf("[BT] Init stub\n");
}

void bt_deinit() {
    printf("[BT] Deinit stub\n");
}

void bt_write(uint8_t *data, uint16_t len) {
    (void)data;
    (void)len;
    // Stub for Bluetooth Write
    // printf("[BT] Writing %d bytes\n", len);
}
