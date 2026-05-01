#include "bt.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

static int bt_socket = -1;

int bt_init() {
    printf("[BT] Initializing RAW HCI socket...\n");

    // Bring down the interface so BlueZ daemon doesn't interfere
    system("hciconfig hci0 down");

    int dev_id = hci_get_route(NULL);
    if (dev_id < 0) {
        perror("[BT] Failed to get local HCI route");
        return -1;
    }

    struct sockaddr_hci a = {0};
    a.hci_family = AF_BLUETOOTH;
    a.hci_dev = dev_id;
    a.hci_channel = HCI_CHANNEL_USER; // Exclusive control

    bt_socket = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (bt_socket < 0) {
        perror("[BT] Failed to create HCI socket");
        return -1;
    }

    if (bind(bt_socket, (struct sockaddr *) &a, sizeof(a)) < 0) {
        perror("[BT] Failed to bind HCI socket to HCI_CHANNEL_USER");
        close(bt_socket);
        bt_socket = -1;
        return -1;
    }

    printf("[BT] Successfully opened exclusive RAW HCI socket (fd: %d) on device %d\n", bt_socket, dev_id);
    return bt_socket;
}

void bt_deinit() {
    if (bt_socket != -1) {
        printf("[BT] Closing socket...\n");
        close(bt_socket);
        bt_socket = -1;
    }
}

void bt_write(uint8_t *data, uint16_t len) {
    if (bt_socket != -1) {
        ssize_t bytes_written = write(bt_socket, data, len);
        if (bytes_written < 0) {
            perror("[BT] Failed to write data");
        } else {
            // printf("[BT] Wrote %zd bytes\n", bytes_written);
        }
    }
}

void bt_handle_data() {
    if (bt_socket == -1) return;

    uint8_t buf[1024];
    ssize_t len = read(bt_socket, buf, sizeof(buf));
    if (len < 0) {
        perror("[BT] Failed to read data");
        return;
    }

    // Phase 3 implementation: just read and debug print
    // printf("[BT] Received %zd bytes from HCI socket\n", len);
    // In Phase 4/full implementation, we would parse L2CAP packets here
}
