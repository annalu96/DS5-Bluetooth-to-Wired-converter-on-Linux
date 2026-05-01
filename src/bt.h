#ifndef DS5_BRIDGE_BT_H
#define DS5_BRIDGE_BT_H

#include <cstdint>

void bt_init();
void bt_write(uint8_t *data, uint16_t len);

#endif //DS5_BRIDGE_BT_H
