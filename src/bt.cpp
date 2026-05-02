#include "bt.h"
#include <cstdio>
#include <cstdlib>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <vector>
#include <string>
#include <dirent.h>

// mgmt API definitions (from kernel's mgmt.h)
#define MGMT_OP_LOAD_LINK_KEYS 0x0012

struct mgmt_hdr {
    uint16_t opcode;
    uint16_t index;
    uint16_t len;
} __attribute__((packed));

struct mgmt_link_key_info {
    bdaddr_t addr;
    uint8_t  addr_type;
    uint8_t  key_type;
    uint8_t  val[16];
    uint8_t  pin_len;
} __attribute__((packed));

struct mgmt_cp_load_link_keys {
    uint8_t  debug_keys;
    uint16_t key_count;
    // followed by key_count * mgmt_link_key_info
} __attribute__((packed));

static int srv_ctrl_fd = -1;
static int srv_intr_fd = -1;
static int ctrl_fd = -1;
static int intr_fd = -1;

static int global_epoll_fd = -1;

static bt_data_callback_t data_callback = nullptr;
static std::vector<std::string> ds5_macs;
static std::string adapter_addr; // e.g. "8A:88:4B:61:A6:A2"

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

// Detect the active adapter address by reading /sys/class/bluetooth/hci0/address
static bool detect_adapter_address() {
    FILE *f = fopen("/sys/class/bluetooth/hci0/address", "r");
    if (!f) {
        printf("[BT] WARNING: Could not read adapter address from sysfs\n");
        return false;
    }
    char buf[32] = {0};
    if (fgets(buf, sizeof(buf), f)) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
        adapter_addr = buf;
    }
    fclose(f);

    if (adapter_addr.empty()) return false;

    // Convert to uppercase for matching /var/lib/bluetooth/ dirs
    for (auto &c : adapter_addr) c = toupper(c);
    printf("[BT] Adapter address: %s\n", adapter_addr.c_str());
    return true;
}

// Parse a hex link key string (32 hex chars) into 16 bytes
static bool parse_hex_key(const char *hex, uint8_t *out) {
    if (strlen(hex) != 32) return false;
    for (int i = 0; i < 16; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return false;
        out[i] = (uint8_t)byte;
    }
    return true;
}

// Load link keys from /var/lib/bluetooth/<adapter>/<device>/info
// and inject them into the kernel via MGMT_OP_LOAD_LINK_KEYS
static bool load_link_keys_from_disk() {
    if (adapter_addr.empty()) {
        printf("[BT] No adapter address known, skipping link key load\n");
        return false;
    }

    std::string base = "/var/lib/bluetooth/" + adapter_addr;
    DIR *dir = opendir(base.c_str());
    if (!dir) {
        printf("[BT] Cannot open %s: %s\n", base.c_str(), strerror(errno));
        return false;
    }

    std::vector<mgmt_link_key_info> keys;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        // Skip . and ..
        if (ent->d_name[0] == '.') continue;
        // Check if it looks like a MAC address (XX:XX:XX:XX:XX:XX)
        if (strlen(ent->d_name) != 17 || ent->d_name[2] != ':') continue;

        std::string info_path = base + "/" + ent->d_name + "/info";
        FILE *f = fopen(info_path.c_str(), "r");
        if (!f) continue;

        char line[256];
        bool in_link_key = false;
        mgmt_link_key_info ki = {};
        bool has_key = false;
        bool has_type = false;

        // Parse the device MAC from directory name
        str2ba(ent->d_name, &ki.addr);
        ki.addr_type = 0x00; // BR/EDR public

        while (fgets(line, sizeof(line), f)) {
            // Strip newline
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';

            if (strcmp(line, "[LinkKey]") == 0) {
                in_link_key = true;
                continue;
            }
            if (line[0] == '[') {
                in_link_key = false;
                continue;
            }

            if (in_link_key) {
                if (strncmp(line, "Key=", 4) == 0) {
                    if (parse_hex_key(line + 4, ki.val)) {
                        has_key = true;
                    }
                } else if (strncmp(line, "Type=", 5) == 0) {
                    ki.key_type = (uint8_t)atoi(line + 5);
                    has_type = true;
                } else if (strncmp(line, "PINLength=", 10) == 0) {
                    ki.pin_len = (uint8_t)atoi(line + 10);
                }
            }
        }
        fclose(f);

        if (has_key && has_type) {
            char addr_str[18];
            ba2str(&ki.addr, addr_str);
            printf("[BT] Loaded link key for %s (type=%d)\n", addr_str, ki.key_type);
            keys.push_back(ki);
        }
    }
    closedir(dir);

    if (keys.empty()) {
        printf("[BT] No link keys found on disk\n");
        return false;
    }

    // Open mgmt socket and send LOAD_LINK_KEYS
    int mgmt_fd = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (mgmt_fd < 0) {
        // Try the mgmt protocol directly
        mgmt_fd = socket(PF_BLUETOOTH, SOCK_RAW | SOCK_NONBLOCK, BTPROTO_HCI);
    }

    // Use the proper mgmt channel
    // The mgmt socket uses AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI
    // with HCI_CHANNEL_CONTROL (3)
    if (mgmt_fd >= 0) close(mgmt_fd);

    mgmt_fd = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (mgmt_fd < 0) {
        printf("[BT] Failed to create mgmt socket: %s\n", strerror(errno));
        return false;
    }

    struct sockaddr_hci mgmt_addr = {};
    mgmt_addr.hci_family = AF_BLUETOOTH;
    mgmt_addr.hci_dev = HCI_DEV_NONE;
    mgmt_addr.hci_channel = HCI_CHANNEL_CONTROL;

    if (bind(mgmt_fd, (struct sockaddr *)&mgmt_addr, sizeof(mgmt_addr)) < 0) {
        printf("[BT] Failed to bind mgmt socket: %s\n", strerror(errno));
        close(mgmt_fd);
        return false;
    }

    // Build the command buffer
    size_t keys_size = keys.size() * sizeof(mgmt_link_key_info);
    size_t cp_size = sizeof(mgmt_cp_load_link_keys) + keys_size;
    size_t total = sizeof(mgmt_hdr) + cp_size;

    std::vector<uint8_t> buf(total, 0);
    mgmt_hdr *hdr = (mgmt_hdr *)buf.data();
    hdr->opcode = htole16(MGMT_OP_LOAD_LINK_KEYS);
    hdr->index = htole16(0); // hci0
    hdr->len = htole16(cp_size);

    mgmt_cp_load_link_keys *cp = (mgmt_cp_load_link_keys *)(buf.data() + sizeof(mgmt_hdr));
    cp->debug_keys = 0;
    cp->key_count = htole16(keys.size());
    memcpy(buf.data() + sizeof(mgmt_hdr) + sizeof(mgmt_cp_load_link_keys),
           keys.data(), keys_size);

    ssize_t written = write(mgmt_fd, buf.data(), total);
    if (written < 0) {
        printf("[BT] Failed to send LOAD_LINK_KEYS: %s\n", strerror(errno));
        close(mgmt_fd);
        return false;
    }

    // Read response
    uint8_t resp[256];
    // Wait briefly for response
    struct pollfd pfd = {mgmt_fd, POLLIN, 0};
    if (poll(&pfd, 1, 2000) > 0) {
        ssize_t r = read(mgmt_fd, resp, sizeof(resp));
        if (r > 0) {
            printf("[BT] LOAD_LINK_KEYS response: %zd bytes\n", r);
        }
    }

    close(mgmt_fd);
    printf("[BT] Loaded %zu link key(s) into kernel\n", keys.size());
    return true;
}

static void bt_takeover_adapter() {
    // Step 1: Disconnect each known DualSense while bluetoothd is still running.
    // This ensures a clean disconnect (not an abrupt drop).
    printf("[BT] Disconnecting known DualSense controllers...\n");
    for (const auto &mac : ds5_macs) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "bluetoothctl disconnect %s 2>/dev/null", mac.c_str());
        system(cmd);
    }
    usleep(300000); // Let disconnects settle

    // Step 2: Stop bluetoothd to free L2CAP PSM 17/19 sockets.
    // bluetoothd's input plugin holds server sockets on those PSMs.
    printf("[BT] Stopping bluetoothd to free L2CAP PSMs...\n");
    system("systemctl stop bluetooth.service 2>/dev/null");
    system("systemctl mask --runtime bluetooth.service 2>/dev/null");
    usleep(500000);

    // Step 3: Unload hid_playstation to prevent it from probing
    // when the DualSense reconnects via BT.
    // Note: hidp is likely not loaded (BlueZ 5.86 uses UHID instead),
    // but we unload it too just in case.
    printf("[BT] Unloading kernel HID modules...\n");
    system("rmmod hid_playstation 2>/dev/null");
    system("rmmod hidp 2>/dev/null");

    // Step 4: Power on the adapter.
    // bluetoothd powered it off on exit, so we re-enable it.
    // The link keys were cleared when the adapter powered off.
    printf("[BT] Powering on adapter...\n");
    system("btmgmt power on 2>/dev/null");
    usleep(500000);

    // Step 5: Reload link keys from /var/lib/bluetooth/ into the kernel.
    // Without these, the DualSense authentication will fail on reconnect.
    detect_adapter_address();
    printf("[BT] Loading link keys from disk...\n");
    load_link_keys_from_disk();

    // Step 6: Make adapter connectable so DualSense can initiate reconnection
    system("btmgmt connectable on 2>/dev/null");
    system("btmgmt bondable on 2>/dev/null");
    system("btmgmt ssp on 2>/dev/null");

    printf("[BT] Adapter ready. Waiting for DualSense reconnection.\n");
}

static void bt_restore_adapter() {
    printf("[BT] Restoring system Bluetooth state...\n");
    // Reload kernel BT HID modules
    system("modprobe hid_playstation 2>/dev/null");
    // Restore bluetoothd
    system("systemctl unmask --runtime bluetooth.service 2>/dev/null");
    system("systemctl start bluetooth.service 2>/dev/null");
    printf("[BT] System Bluetooth state restored.\n");
}

void bt_reload_hid_playstation() {
    // Reload hid_playstation so the USB gadget (which uses Sony VID/PID)
    // is recognized as a proper DualSense controller by the host.
    // We do NOT reload hidp — it must stay unloaded to prevent the kernel
    // from intercepting future BT HID reconnections.
    printf("[BT] Reloading hid_playstation for USB gadget recognition...\n");
    system("modprobe hid_playstation 2>/dev/null");
    usleep(200000); // Give kernel time to load the module
    printf("[BT] hid_playstation reloaded.\n");
}

bool bt_is_connected() {
    return ctrl_fd >= 0 && intr_fd >= 0;
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
    // This disconnects controllers, stops bluetoothd, unloads modules,
    // then powers on the adapter and loads link keys from disk.
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
