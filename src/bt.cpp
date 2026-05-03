#include "bt.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <poll.h>
#include <string>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

static int hidraw_fd = -1;
static int global_epoll_fd = -1;
static bt_data_callback_t data_callback = nullptr;

// evdev file descriptors grabbed with EVIOCGRAB to prevent double input
static std::vector<int> grabbed_evdev_fds;

// Path to the hidraw device we opened (e.g. "/dev/hidraw3")
static std::string hidraw_path;

void bt_register_data_callback(bt_data_callback_t callback) {
  data_callback = callback;
}

// Scan /sys/class/hidraw/ to find the DualSense hidraw device node.
// Returns the path (e.g. "/dev/hidraw3") or empty string on failure.
static std::string find_hidraw_device() {
  // DualSense standard:  VID=054C PID=0CE6
  // DualSense Edge:      VID=054C PID=0DF2
  const char *target_vid = "054C";
  const char *target_pids[] = {"0CE6", "0DF2", NULL};

  DIR *dir = opendir("/sys/class/hidraw");
  if (!dir) {
    printf("[BT] Cannot open /sys/class/hidraw: %s\n", strerror(errno));
    return "";
  }

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;

    // Read the uevent file to get HID_ID (contains bus, VID, PID)
    char uevent_path[256];
    snprintf(uevent_path, sizeof(uevent_path),
             "/sys/class/hidraw/%s/device/uevent", ent->d_name);

    FILE *f = fopen(uevent_path, "r");
    if (!f)
      continue;

    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
      // HID_ID line format: HID_ID=0005:0000054C:00000CE6
      // (bus:vendor:product in zero-padded hex)
      char *hid_id = strstr(line, "HID_ID=");
      if (!hid_id)
        continue;

      // Check for our VID (case-insensitive)
      if (strcasestr(hid_id, target_vid)) {
        for (int i = 0; target_pids[i]; i++) {
          if (strcasestr(hid_id, target_pids[i])) {
            found = true;
            break;
          }
        }
      }
    }
    fclose(f);

    if (found) {
      std::string dev_path = std::string("/dev/") + ent->d_name;
      printf("[BT] 🎮 DualSense encontrado em: %s\n", dev_path.c_str());
      closedir(dir);
      return dev_path;
    }
  }

  closedir(dir);
  return "";
}

// Find and EVIOCGRAB all evdev nodes associated with the same HID device.
// This prevents the "real" DualSense input events from reaching the system
// (avoiding double input when the USB gadget virtual device is active).
static void grab_evdev_devices() {
  // Get the sysfs device path for our hidraw
  // /sys/class/hidraw/hidrawX/device -> ../../../0005:054C:0CE6.XXXX
  // The evdev nodes are siblings under the same HID device.

  // Extract hidraw name from path (e.g. "hidraw3" from "/dev/hidraw3")
  std::string hidraw_name = hidraw_path.substr(5); // remove "/dev/"

  char device_link[512];
  char sysfs_path[256];
  snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/hidraw/%s/device",
           hidraw_name.c_str());

  ssize_t len = readlink(sysfs_path, device_link, sizeof(device_link) - 1);
  if (len < 0) {
    printf("[BT] ⚠️ Não foi possível ler o link sysfs de %s: %s\n",
           sysfs_path, strerror(errno));
    return;
  }
  device_link[len] = '\0';

  // Resolve to absolute path
  char abs_device_path[512];
  snprintf(abs_device_path, sizeof(abs_device_path),
           "/sys/class/hidraw/%s/%s", hidraw_name.c_str(), device_link);

  // Now scan /dev/input/eventX to find nodes belonging to this HID device.
  // We check /sys/class/input/eventX/device -> points to the same HID device
  DIR *input_dir = opendir("/sys/class/input");
  if (!input_dir) {
    printf("[BT] ⚠️ Não foi possível abrir /sys/class/input\n");
    return;
  }

  // Get the HID device ID from our hidraw uevent to match against input devices
  char uevent_path[256];
  snprintf(uevent_path, sizeof(uevent_path),
           "/sys/class/hidraw/%s/device/uevent", hidraw_name.c_str());
  FILE *ue = fopen(uevent_path, "r");
  std::string hid_name_from_uevent;
  if (ue) {
    char line[256];
    while (fgets(line, sizeof(line), ue)) {
      if (strncmp(line, "HID_NAME=", 9) == 0) {
        char *nl = strchr(line + 9, '\n');
        if (nl)
          *nl = '\0';
        hid_name_from_uevent = line + 9;
        break;
      }
    }
    fclose(ue);
  }

  struct dirent *ent;
  while ((ent = readdir(input_dir)) != NULL) {
    if (strncmp(ent->d_name, "event", 5) != 0)
      continue;

    // Check the name of this input device
    char name_path[256];
    snprintf(name_path, sizeof(name_path),
             "/sys/class/input/%s/device/name", ent->d_name);

    FILE *f = fopen(name_path, "r");
    if (!f)
      continue;

    char name[128] = {0};
    fgets(name, sizeof(name), f);
    fclose(f);

    char *nl = strchr(name, '\n');
    if (nl)
      *nl = '\0';

    // Match by device name — DualSense devices contain "DualSense" or
    // "Sony Interactive Entertainment" in their name
    bool matches = false;
    if (strcasestr(name, "DualSense") || strcasestr(name, "Wireless Controller")) {
      matches = true;
    }
    // Also match by HID_NAME if available
    if (!hid_name_from_uevent.empty() &&
        strcasestr(name, hid_name_from_uevent.c_str())) {
      matches = true;
    }

    if (!matches)
      continue;

    char dev_path[64];
    snprintf(dev_path, sizeof(dev_path), "/dev/input/%s", ent->d_name);

    int evfd = open(dev_path, O_RDONLY | O_NONBLOCK);
    if (evfd < 0) {
      printf("[BT] ⚠️ Não foi possível abrir %s: %s\n", dev_path,
             strerror(errno));
      continue;
    }

    if (ioctl(evfd, EVIOCGRAB, 1) < 0) {
      printf("[BT] ⚠️ EVIOCGRAB falhou em %s: %s\n", dev_path,
             strerror(errno));
      close(evfd);
      continue;
    }

    printf("[BT] 🔒 EVIOCGRAB em %s (%s) — input do controle real suprimido\n",
           dev_path, name);
    grabbed_evdev_fds.push_back(evfd);
  }

  closedir(input_dir);

  if (grabbed_evdev_fds.empty()) {
    printf("[BT] ⚠️ Nenhum dispositivo evdev do DualSense encontrado para "
           "grab.\n");
    printf("[BT]    O sistema pode receber input duplo (real + virtual).\n");
  }
}

// Release all EVIOCGRAB locks
static void release_evdev_grabs() {
  for (int fd : grabbed_evdev_fds) {
    ioctl(fd, EVIOCGRAB, 0);
    close(fd);
  }
  grabbed_evdev_fds.clear();
}

static void add_to_epoll(int fd) {
  if (global_epoll_fd == -1 || fd == -1)
    return;
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = fd;
  epoll_ctl(global_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

static void remove_from_epoll(int fd) {
  if (global_epoll_fd == -1 || fd == -1)
    return;
  epoll_ctl(global_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

bool bt_is_connected() { return hidraw_fd >= 0; }

int bt_init(int epoll_fd) {
  global_epoll_fd = epoll_fd;

  printf("[BT] Procurando DualSense via hidraw...\n");

  // Try to find the device, with retries (user may not have connected yet)
  for (int attempt = 1; attempt <= 1; attempt++) {
    hidraw_path = find_hidraw_device();
    if (!hidraw_path.empty())
      break;
    printf("[BT] DualSense não encontrado (tentativa %d). Aguarde a conexão "
           "Bluetooth...\n",
           attempt);
    sleep(2);
  }

  if (hidraw_path.empty()) {
    printf("\n[BT] ❌ Nenhum DualSense encontrado.\n");
    printf("[BT] 👉 Conecte o controle via Bluetooth e tente novamente.\n");
    printf("[BT] 👉 Use: bluetoothctl connect <MAC>\n");
    return -1;
  }

  // Open the hidraw device for reading and writing
  hidraw_fd = open(hidraw_path.c_str(), O_RDWR | O_NONBLOCK);
  if (hidraw_fd < 0) {
    printf("[BT] ❌ Falha ao abrir %s: %s\n", hidraw_path.c_str(),
           strerror(errno));
    printf("[BT] 👉 Verifique permissões (execute como root ou configure "
           "udev).\n");
    return -1;
  }

  // Print device info
  struct hidraw_devinfo info;
  if (ioctl(hidraw_fd, HIDIOCGRAWINFO, &info) == 0) {
    printf("[BT] ✅ DualSense aberto: bus=%d vendor=0x%04hX product=0x%04hX\n",
           info.bustype, info.vendor, info.product);
  }

  char name[256] = {0};
  if (ioctl(hidraw_fd, HIDIOCGRAWNAME(sizeof(name)), name) > 0) {
    printf("[BT]    Nome: %s\n", name);
  }

  // Grab evdev devices to prevent double input
  grab_evdev_devices();

  // Add hidraw to epoll for monitoring incoming HID reports
  add_to_epoll(hidraw_fd);

  printf("[BT] ✅ Bridge hidraw inicializado com sucesso.\n");
  return 0;
}

void bt_deinit() {
  printf("[BT] Encerrando bridge hidraw...\n");

  // Release evdev grabs
  release_evdev_grabs();

  // Close hidraw
  if (hidraw_fd >= 0) {
    remove_from_epoll(hidraw_fd);
    close(hidraw_fd);
    hidraw_fd = -1;
  }

  hidraw_path.clear();
  printf("[BT] Bridge hidraw encerrado.\n");
}

void bt_write(CHANNEL_TYPE channel, uint8_t *data, uint16_t len) {
  if (hidraw_fd < 0)
    return;

  if (channel == INTERRUPT) {
    // For output reports via hidraw, we send the raw HID report
    // WITHOUT the L2CAP transaction header (0xA2).
    // hidraw expects: [report_id] [data...]
    //
    // The caller currently sends: [A2] [31] [seq] [data...]
    // We need to strip the 0xA2 header and send [31] [seq] [data...]
    if (len > 1 && data[0] == 0xA2) {
      ssize_t written = write(hidraw_fd, data + 1, len - 1);
      if (written < 0 && errno != EAGAIN) {
        printf("[BT] ❌ Falha ao escrever no hidraw: %s\n", strerror(errno));
      }
    } else {
      // Already in raw format (no A2 prefix)
      ssize_t written = write(hidraw_fd, data, len);
      if (written < 0 && errno != EAGAIN) {
        printf("[BT] ❌ Falha ao escrever no hidraw: %s\n", strerror(errno));
      }
    }
  } else if (channel == CONTROL) {
    // For control channel (feature reports), use ioctl instead
    // The caller sends: [0x53] [report_id] [data...] (SET_REPORT)
    //                or: [0x43] [report_id]          (GET_REPORT)
    if (len >= 2 && data[0] == 0x53) {
      // SET_REPORT (Feature): strip the 0x53 header, use HIDIOCSFEATURE
      // hidraw expects: [report_id] [data...]
      int ret = ioctl(hidraw_fd, HIDIOCSFEATURE(len - 1), data + 1);
      if (ret < 0) {
        printf("[BT] ⚠️ HIDIOCSFEATURE falhou: %s\n", strerror(errno));
      }
    } else if (len >= 2 && data[0] == 0x43) {
      // GET_REPORT (Feature): this is a request, we handle it via
      // bt_get_feature_report() instead
      // Ignore here — usb.cpp will call bt_get_feature_report() directly
    }
  }
}

int bt_get_feature_report(uint8_t report_id, uint8_t *buf, size_t buf_len) {
  if (hidraw_fd < 0)
    return -1;

  // hidraw HIDIOCGFEATURE: buf[0] = report_id, kernel fills the rest
  buf[0] = report_id;
  int ret = ioctl(hidraw_fd, HIDIOCGFEATURE(buf_len), buf);
  if (ret < 0) {
    printf("[BT] ⚠️ HIDIOCGFEATURE(0x%02x) falhou: %s\n", report_id,
           strerror(errno));
    return -1;
  }
  return ret;
}

int bt_set_feature_report(uint8_t *data, size_t len) {
  if (hidraw_fd < 0)
    return -1;

  // data[0] = report_id, data[1..] = report data
  int ret = ioctl(hidraw_fd, HIDIOCSFEATURE(len), data);
  if (ret < 0) {
    printf("[BT] ⚠️ HIDIOCSFEATURE falhou: %s\n", strerror(errno));
    return -1;
  }
  return ret;
}

int bt_get_hidraw_fd() { return hidraw_fd; }

bool bt_is_fd_mine(int fd) {
  if (fd < 0)
    return false;
  return fd == hidraw_fd;
}

void bt_process_epoll_event(int fd) {
  if (fd != hidraw_fd)
    return;

  // Read HID report from hidraw
  // Bluetooth DualSense reports: [31] [seq] [data...] (up to ~78 bytes)
  uint8_t buf[256];
  ssize_t len = read(hidraw_fd, buf, sizeof(buf));

  if (len <= 0) {
    if (len == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
      printf("[BT] ❌ Conexão hidraw perdida (controle desconectou?).\n");
      remove_from_epoll(hidraw_fd);
      close(hidraw_fd);
      hidraw_fd = -1;
    }
    return;
  }

  if (len > 0 && data_callback) {
    // hidraw gives us raw HID reports: [report_id] [data...]
    // For input reports (0x31), we prepend 0xA1 to match the existing
    // on_bt_data() format that main.cpp expects:
    //   [A1] [31] [seq] [data...]
    //
    // This maintains compatibility with the existing translation code.
    if (buf[0] == 0x31) {
      uint8_t framed[257];
      framed[0] = 0xA1; // HID DATA header (input report)
      memcpy(framed + 1, buf, len);
      data_callback(INTERRUPT, framed, len + 1);
    } else if (buf[0] == 0x01) {
      // USB-mode input report (shouldn't happen over BT hidraw, but handle it)
      uint8_t framed[257];
      framed[0] = 0xA1;
      memcpy(framed + 1, buf, len);
      data_callback(INTERRUPT, framed, len + 1);
    }
    // Feature report responses would come via ioctl, not read()
  }
}
