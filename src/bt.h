#ifndef DS5_BRIDGE_BT_H
#define DS5_BRIDGE_BT_H

#include <cstdint>

int bt_init();
void bt_deinit();
void bt_write(uint8_t *data, uint16_t len);
void bt_handle_data();

#endif //DS5_BRIDGE_BT_H
