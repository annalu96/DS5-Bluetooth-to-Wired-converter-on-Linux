#include "bt.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <vector>
#include <string>

static int srv_ctrl_fd = -1;
static int srv_intr_fd = -1;
static int ctrl_fd = -1;
static int intr_fd = -1;

static int global_epoll_fd = -1;

static bt_data_callback_t data_callback = nullptr;
static std::vector<std::string> ds5_macs;

void bt_register_data_callback(bt_data_callback_t callback) {
    data_callback = callback;
}

static bool load_ds5_macs() {
    const char *paths[] = {"./ds5_mac.txt", "../ds5_mac.txt", NULL};
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "r");
        if (!f) continue;
        char line[64];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            nl = strchr(line, '\r');
            if (nl) *nl = '\0';
            if (strlen(line) == 17 && line[2] == ':' && line[5] == ':') {
                ds5_macs.push_back(std::string(line));
            }
        }
        fclose(f);
        return !ds5_macs.empty();
    }
    return false;
}

static bool is_allowed_mac(const bdaddr_t *addr) {
    if (ds5_macs.empty()) return true; // No filter = accept all
    char addr_str[18];
    ba2str(addr, addr_str);
    for (const auto &mac : ds5_macs) {
        if (strcasecmp(addr_str, mac.c_str()) == 0) return true;
    }
    return false;
}

static int create_l2cap_listen_socket(uint16_t psm) {
    int sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (sock < 0) {
        printf("[BT] Failed to create L2CAP socket for PSM 0x%02X: %s\n", psm, strerror(errno));
        return -1;
    }

    struct sockaddr_l2 addr = {0};
    addr.l2_family = AF_BLUETOOTH;
    addr.l2_bdaddr = (bdaddr_t) {{0, 0, 0, 0, 0, 0}};
    addr.l2_psm = htobs(psm);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[BT] Failed to bind L2CAP socket for PSM 0x%02X: %s\n", psm, strerror(errno));
        close(sock);
        return -1;
    }

    if (listen(sock, 1) < 0) {
        printf("[BT] Failed to listen on L2CAP socket for PSM 0x%02X: %s\n", psm, strerror(errno));
        close(sock);
        return -1;
    }

    // Set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    return sock;
}

static void add_to_epoll(int fd) {
    if (global_epoll_fd == -1 || fd == -1) return;
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(global_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

static void remove_from_epoll(int fd) {
    if (global_epoll_fd == -1 || fd == -1) return;
    epoll_ctl(global_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

static void bt_takeover_adapter() {
    // Mask bluetooth.service to prevent systemd from restarting it,
    // then stop it. --runtime makes the mask temporary (cleared on reboot).
    printf("[BT] Stopping and masking bluetoothd to take exclusive control of adapter...\n");
    system("systemctl mask --runtime bluetooth.service 2>/dev/null");
    system("systemctl stop bluetooth.service 2>/dev/null");
    usleep(500000); // Wait for bluetoothd to fully stop

    // Power cycle the HCI adapter to drop ALL kernel-level BT connections.
    // This naturally disconnects any paired controllers (they power off).
    // No need for explicit disconnect — the power cycle handles it.
    printf("[BT] Power-cycling HCI adapter...\n");
    system("btmgmt power off 2>/dev/null");
    usleep(300000);
    system("btmgmt power on 2>/dev/null");
    usleep(300000);

    // Make adapter connectable so DualSense can initiate connection to us
    system("btmgmt connectable on 2>/dev/null");
    system("btmgmt bondable on 2>/dev/null");

    printf("[BT] Adapter hci0 under exclusive control (bluetoothd masked and stopped).\n");
}

static void bt_restore_adapter() {
    printf("[BT] Restoring bluetoothd...\n");
    system("systemctl unmask --runtime bluetooth.service 2>/dev/null");
    system("systemctl start bluetooth.service 2>/dev/null");
    printf("[BT] bluetoothd restored.\n");
}

int bt_init(int epoll_fd) {
    global_epoll_fd = epoll_fd;

    // Load DualSense MAC addresses from config file
    if (load_ds5_macs()) {
        printf("[BT] Loaded %zu DualSense MAC(s):\n", ds5_macs.size());
        for (const auto &mac : ds5_macs) {
            printf("[BT]   %s\n", mac.c_str());
        }
    } else {
        printf("[BT] WARNING: No MAC addresses found in ds5_mac.txt\n");
        printf("[BT] Will accept connections from ANY device.\n");
        printf("[BT] Create ds5_mac.txt with DualSense MACs (one per line, format: AA:BB:CC:DD:EE:FF)\n");
    }

    // Take exclusive control of the BT adapter before creating sockets.
    // This prevents BlueZ from intercepting the DualSense connection.
    bt_takeover_adapter();

    printf("[BT] Initializing L2CAP sockets...\n");

    srv_ctrl_fd = create_l2cap_listen_socket(17); // PSM_HID_CONTROL
    if (srv_ctrl_fd < 0) {
        bt_restore_adapter();
        return -1;
    }
    add_to_epoll(srv_ctrl_fd);

    srv_intr_fd = create_l2cap_listen_socket(19); // PSM_HID_INTERRUPT
    if (srv_intr_fd < 0) {
        close(srv_ctrl_fd);
        bt_restore_adapter();
        return -1;
    }
    add_to_epoll(srv_intr_fd);

    printf("[BT] Listening for DualSense connection on PSM 17 and 19...\n");
    return 0; // Return success, we no longer return a single fd
}

void bt_deinit() {
    printf("[BT] Closing sockets...\n");
    if (ctrl_fd != -1) close(ctrl_fd);
    if (intr_fd != -1) close(intr_fd);
    if (srv_ctrl_fd != -1) close(srv_ctrl_fd);
    if (srv_intr_fd != -1) close(srv_intr_fd);

    ctrl_fd = -1;
    intr_fd = -1;
    srv_ctrl_fd = -1;
    srv_intr_fd = -1;

    // Restore bluetoothd so the system returns to normal
    bt_restore_adapter();
}

void bt_write(CHANNEL_TYPE channel, uint8_t *data, uint16_t len) {
    int fd = (channel == INTERRUPT) ? intr_fd : ctrl_fd;
    if (fd != -1) {
        ssize_t bytes_written = write(fd, data, len);
        if (bytes_written < 0) {
            printf("[BT] Failed to write data: %s\n", strerror(errno));
        }
    }
}

bool bt_is_fd_mine(int fd) {
    if (fd < 0) return false;
    return fd == srv_ctrl_fd || fd == srv_intr_fd || fd == ctrl_fd || fd == intr_fd;
}

void bt_process_epoll_event(int fd) {
    if (fd == srv_ctrl_fd) {
        struct sockaddr_l2 rem_addr = {0};
        socklen_t opt = sizeof(rem_addr);
        int client = accept(srv_ctrl_fd, (struct sockaddr *)&rem_addr, &opt);
        if (client >= 0) {
            if (!is_allowed_mac(&rem_addr.l2_bdaddr)) {
                char addr_str[18];
                ba2str(&rem_addr.l2_bdaddr, addr_str);
                printf("[BT] Rejected Control connection from unknown device %s\n", addr_str);
                close(client);
                return;
            }
            char addr_str[18];
            ba2str(&rem_addr.l2_bdaddr, addr_str);
            printf("[BT] Accepted Control Connection from %s\n", addr_str);
            if (ctrl_fd != -1) {
                remove_from_epoll(ctrl_fd);
                close(ctrl_fd);
            }
            ctrl_fd = client;
            int flags = fcntl(ctrl_fd, F_GETFL, 0);
            fcntl(ctrl_fd, F_SETFL, flags | O_NONBLOCK);
            add_to_epoll(ctrl_fd);
        }
    }
    else if (fd == srv_intr_fd) {
        struct sockaddr_l2 rem_addr = {0};
        socklen_t opt = sizeof(rem_addr);
        int client = accept(srv_intr_fd, (struct sockaddr *)&rem_addr, &opt);
        if (client >= 0) {
            if (!is_allowed_mac(&rem_addr.l2_bdaddr)) {
                char addr_str[18];
                ba2str(&rem_addr.l2_bdaddr, addr_str);
                printf("[BT] Rejected Interrupt connection from unknown device %s\n", addr_str);
                close(client);
                return;
            }
            char addr_str[18];
            ba2str(&rem_addr.l2_bdaddr, addr_str);
            printf("[BT] Accepted Interrupt Connection from %s\n", addr_str);
            if (intr_fd != -1) {
                remove_from_epoll(intr_fd);
                close(intr_fd);
            }
            intr_fd = client;
            int flags = fcntl(intr_fd, F_GETFL, 0);
            fcntl(intr_fd, F_SETFL, flags | O_NONBLOCK);
            add_to_epoll(intr_fd);
        }
    }
    else if (fd == ctrl_fd || fd == intr_fd) {
        uint8_t buf[1024];
        ssize_t len = read(fd, buf, sizeof(buf));
        if (len <= 0) {
            if (len == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                printf("[BT] Connection closed or error. Disconnecting.\n");
                remove_from_epoll(fd);
                close(fd);
                if (fd == ctrl_fd) ctrl_fd = -1;
                if (fd == intr_fd) intr_fd = -1;
            }
            return;
        }

        if (len > 0 && data_callback) {
            data_callback(fd == intr_fd ? INTERRUPT : CONTROL, buf, len);
        }
    }
}
