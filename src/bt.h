#ifndef DS5_BRIDGE_BT_H
#define DS5_BRIDGE_BT_H

#include <cstdint>

typedef void (*bt_data_callback_t)(uint8_t *data, uint16_t len);

int bt_init();
void bt_deinit();
void bt_write(uint8_t packet_type, uint8_t *data, uint16_t len);
void bt_handle_data();
void bt_register_data_callback(bt_data_callback_t callback);

#endif //DS5_BRIDGE_BT_H
