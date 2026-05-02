#ifndef DS5_BRIDGE_BT_H
#define DS5_BRIDGE_BT_H

#include <cstdint>

enum CHANNEL_TYPE {
    INTERRUPT,
    CONTROL
};

typedef void (*bt_data_callback_t)(CHANNEL_TYPE channel, uint8_t *data, uint16_t len);

int bt_init(int epoll_fd);
void bt_deinit();
void bt_write(CHANNEL_TYPE channel, uint8_t *data, uint16_t len);
void bt_register_data_callback(bt_data_callback_t callback);
void bt_process_epoll_event(int fd);
bool bt_is_fd_mine(int fd);
void bt_reload_hid_playstation();
bool bt_is_connected();

#endif //DS5_BRIDGE_BT_H
