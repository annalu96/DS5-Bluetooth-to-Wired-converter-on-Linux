#include <endian.h>
#include "usb.h"
#include "usb_descriptors.h"

#include <linux/usb/functionfs.h>
#include <linux/hid.h>
#include <linux/usb/ch9.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <map>
#include <vector>
#include "bt.h"
#include "utils.h"

uint8_t mute[2] = {0}; // 0: SPEAKER(0x02) 1: MIC(0x05)
float volume[2] = {1.0f, 1.0f}; // 0: SPEAKER(0x02) 1: MIC(0x05)

int ep0_fd = -1;
int ep_audio_out_fd = -1;
int ep_audio_in_fd = -1;
int ep_hid_in_fd = -1;
int ep_hid_out_fd = -1;

extern std::map<uint8_t, std::vector<uint8_t>> feature_data;

struct usb_ctrlrequest current_setup;
bool feature_pending = false;

// HID descriptor structure for FunctionFS
struct usb_hid_descriptor {
    __u8  bLength;
    __u8  bDescriptorType;
    __le16 bcdHID;
    __u8  bCountryCode;
    __u8  bNumDescriptors;
    __u8  bReportDescriptorType;
    __le16 wDescriptorLength;
} __attribute__((packed));

struct usb_ext_descriptors {
    struct usb_functionfs_descs_head_v2 header;
    __le32 fs_count;
    __le32 hs_count;

    uint8_t fs_desc[231];
    uint8_t hs_desc[231];
} __attribute__((packed));

struct usb_ext_descriptors descriptors = {
    .header = {
        .magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
        .length = htole32(sizeof(descriptors)),
        .flags = htole32(FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC),
    },
    .fs_count = htole32(26),
    .hs_count = htole32(26),
    .fs_desc = {
        0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
        0x01, 0x0A, 0x24, 0x24, 0x01,
        0x01, 0x00, 0x01, 0x49, 0x00, 0x00, 0x02, 0x01,
        0x02, 0x0C, 0x24, 0x02, 0x01, 0x01, 0x01, 0x01,
        0x06, 0x04, 0x33, 0x00, 0x00, 0x00, 0x00, 0x0C,
        0x24, 0x06, 0x02, 0x01, 0x01, 0x03, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x09, 0x24, 0x03, 0x03, 0x01,
        0x03, 0x03, 0x04, 0x02, 0x00, 0x0C, 0x24, 0x02,
        0x04, 0x02, 0x04, 0x04, 0x03, 0x02, 0x03, 0x00,
        0x00, 0x00, 0x00, 0x09, 0x24, 0x06, 0x05, 0x04,
        0x01, 0x03, 0x00, 0x00, 0x09, 0x24, 0x03, 0x06,
        0x01, 0x01, 0x01, 0x01, 0x05, 0x00, 0x09, 0x04,
        0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x09,
        0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,
        0x07, 0x24, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,
        0x0B, 0x24, 0x02, 0x01, 0x04, 0x02, 0x10, 0x01,
        0x80, 0xBB, 0x00, 0x00, 0x09, 0x05, 0x01, 0x09,
        0x88, 0x01, 0x01, 0x00, 0x00, 0x07, 0x25, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x09, 0x04, 0x02, 0x00,
        0x00, 0x01, 0x02, 0x00, 0x00, 0x09, 0x04, 0x02,
        0x01, 0x01, 0x01, 0x02, 0x00, 0x00, 0x07, 0x24,
        0x01, 0x06, 0x01, 0x01, 0x00, 0x00, 0x0B, 0x24,
        0x02, 0x01, 0x02, 0x02, 0x10, 0x01, 0x80, 0xBB,
        0x00, 0x09, 0x05, 0x82, 0x05, 0xC4, 0x00, 0x01,
        0x00, 0x00, 0x07, 0x25, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x09, 0x04, 0x03, 0x00, 0x02, 0x03, 0x00,
        0x00, 0x00, 0x09, 0x21, 0x11, 0x01, 0x00, 0x01,
        0x22, 0x21, 0x01, 0x01, 0x07, 0x05, 0x84, 0x03,
        0x40, 0x00, 0x01, 0x07, 0x05, 0x03, 0x03, 0x40,
        0x00, 0x01,

    },
    .hs_desc = {
        0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
        0x01, 0x0A, 0x24, 0x24, 0x01,
        0x01, 0x00, 0x01, 0x49, 0x00, 0x00, 0x02, 0x01,
        0x02, 0x0C, 0x24, 0x02, 0x01, 0x01, 0x01, 0x01,
        0x06, 0x04, 0x33, 0x00, 0x00, 0x00, 0x00, 0x0C,
        0x24, 0x06, 0x02, 0x01, 0x01, 0x03, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x09, 0x24, 0x03, 0x03, 0x01,
        0x03, 0x03, 0x04, 0x02, 0x00, 0x0C, 0x24, 0x02,
        0x04, 0x02, 0x04, 0x04, 0x03, 0x02, 0x03, 0x00,
        0x00, 0x00, 0x00, 0x09, 0x24, 0x06, 0x05, 0x04,
        0x01, 0x03, 0x00, 0x00, 0x09, 0x24, 0x03, 0x06,
        0x01, 0x01, 0x01, 0x01, 0x05, 0x00, 0x09, 0x04,
        0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x09,
        0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,
        0x07, 0x24, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,
        0x0B, 0x24, 0x02, 0x01, 0x04, 0x02, 0x10, 0x01,
        0x80, 0xBB, 0x00, 0x00, 0x09, 0x05, 0x01, 0x09,
        0x88, 0x01, 0x01, 0x00, 0x00, 0x07, 0x25, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x09, 0x04, 0x02, 0x00,
        0x00, 0x01, 0x02, 0x00, 0x00, 0x09, 0x04, 0x02,
        0x01, 0x01, 0x01, 0x02, 0x00, 0x00, 0x07, 0x24,
        0x01, 0x06, 0x01, 0x01, 0x00, 0x00, 0x0B, 0x24,
        0x02, 0x01, 0x02, 0x02, 0x10, 0x01, 0x80, 0xBB,
        0x00, 0x09, 0x05, 0x82, 0x05, 0xC4, 0x00, 0x01,
        0x00, 0x00, 0x07, 0x25, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x09, 0x04, 0x03, 0x00, 0x02, 0x03, 0x00,
        0x00, 0x00, 0x09, 0x21, 0x11, 0x01, 0x00, 0x01,
        0x22, 0x21, 0x01, 0x01, 0x07, 0x05, 0x84, 0x03,
        0x40, 0x00, 0x01, 0x07, 0x05, 0x03, 0x03, 0x40,
        0x00, 0x01,

    }
};


struct {
    struct usb_functionfs_strings_head header;
    struct {
        __le16 code;
        const char str1[32]; // "DualSense HID"
    } __attribute__((packed)) lang0;
} __attribute__((packed)) strings = {
    .header = {
        .magic = htole32(FUNCTIONFS_STRINGS_MAGIC),
        .length = htole32(sizeof(strings)),
        .str_count = htole32(1),
        .lang_count = htole32(1),
    },
    .lang0 = {
        .code = htole16(0x0409),
        .str1 = "DualSense HID",
    },
};

int usb_init() {
    printf("[USB] Running setup_gadget.sh...\n");
    if (system("./setup_gadget.sh") != 0 && system("../setup_gadget.sh") != 0) {
        printf("[USB] Failed to run setup_gadget.sh. Make sure you are root and the script is in the current or parent directory.\n");
        // return -1;
    }

    // Since we dynamically initialized desc_hid_report_ds_len, we need to set wDescriptorLength
    descriptors.fs_desc[214] = desc_hid_report_ds_len & 0xFF;
    descriptors.fs_desc[215] = (desc_hid_report_ds_len >> 8) & 0xFF;
    descriptors.hs_desc[214] = desc_hid_report_ds_len & 0xFF;
    descriptors.hs_desc[215] = (desc_hid_report_ds_len >> 8) & 0xFF;

    printf("[USB] Opening FFS ep0...\n");
    ep0_fd = open("/dev/ffs/ep0", O_RDWR);
    if (ep0_fd < 0) {
        perror("[USB] Failed to open ep0");
        return -1;
    }

    printf("[USB] Writing descriptors...\n");
    if (write(ep0_fd, &descriptors, sizeof(descriptors)) < 0) {
        perror("[USB] Failed to write descriptors");
        return -1;
    }

    printf("[USB] Writing strings...\n");
    if (write(ep0_fd, &strings, sizeof(strings)) < 0) {
        perror("[USB] Failed to write strings");
        return -1;
    }

    printf("[USB] Descriptors and strings written. Opening endpoints...\n");

    ep_audio_out_fd = open("/dev/ffs/ep1", O_RDWR | O_NONBLOCK);
    if (ep_audio_out_fd < 0) {
        perror("[USB] Failed to open ep1 (Audio OUT)");
        return -1;
    }

    ep_audio_in_fd = open("/dev/ffs/ep2", O_RDWR | O_NONBLOCK);
    if (ep_audio_in_fd < 0) {
        perror("[USB] Failed to open ep2 (Audio IN)");
        return -1;
    }


    ep_hid_in_fd = open("/dev/ffs/ep3", O_RDWR | O_NONBLOCK);
    if (ep_hid_in_fd < 0) {
        perror("[USB] Failed to open ep3 (HID IN)");
        return -1;
    }

    ep_hid_out_fd = open("/dev/ffs/ep4", O_RDWR | O_NONBLOCK);
    if (ep_hid_out_fd < 0) {
        perror("[USB] Failed to open ep4 (HID OUT)");
        return -1;
    }


    printf("[USB] Binding gadget to dummy UDC...\n");
    if (system("echo dummy_udc.0 > /sys/kernel/config/usb_gadget/dualsense/UDC") != 0) {
        printf("[USB] Failed to bind UDC.\n");
    } else {
        printf("[USB] Gadget successfully bound.\n");
    }

    return 0;
}

void usb_deinit() {
    if (ep_audio_out_fd >= 0) close(ep_audio_out_fd);
    if (ep_audio_in_fd >= 0) close(ep_audio_in_fd);
    if (ep_hid_in_fd >= 0) close(ep_hid_in_fd);
    if (ep_hid_out_fd >= 0) close(ep_hid_out_fd);
    if (ep0_fd >= 0) close(ep0_fd);

    system("echo \"\" > /sys/kernel/config/usb_gadget/dualsense/UDC 2>/dev/null");
}

#ifndef HID_REQ_GET_REPORT
#define HID_REQ_GET_REPORT		0x01
#define HID_REQ_GET_IDLE		0x02
#define HID_REQ_GET_PROTOCOL		0x03
#define HID_REQ_SET_REPORT		0x09
#define HID_REQ_SET_IDLE		0x0A
#define HID_REQ_SET_PROTOCOL		0x0B
#endif

void usb_handle_ep0() {
    struct usb_ctrlrequest setup;
    int ret = read(ep0_fd, &setup, sizeof(setup));
    if (ret < 0) {
        // Not all kernels pass all setup packets.
        // A read error might just mean there's no valid setup packet pending or an interruption.
        return;
    }

    if (ret == 0) {
        printf("[USB] ep0 closed?!\n");
        return;
    }

    if (setup.bRequestType & USB_DIR_IN) {
        // Host is reading from us
        if ((setup.bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
            if (setup.bRequest == USB_REQ_GET_DESCRIPTOR) {
                if ((setup.wValue >> 8) == HID_DT_REPORT) {
                    // printf("[USB] Sending HID Report Descriptor...\n");
                    write(ep0_fd, desc_hid_report_ds, desc_hid_report_ds_len);
                } else {
                    // Stall other descriptor requests we don't handle
                    // FunctionFS handles most standard requests internally anyway
                    // This is for ones explicitly forwarded to user space
                    read(ep0_fd, NULL, 0);
                }
            } else {
                 read(ep0_fd, NULL, 0);
            }
        } else if ((setup.bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS) {
            if (setup.bRequest == HID_REQ_GET_REPORT) {
                 uint8_t report_type = setup.wValue >> 8;
                 uint8_t report_id = setup.wValue & 0xFF;

                 if (report_type == 3) { // Feature Report
                     if (feature_data.find(report_id) != feature_data.end()) {
                         // We have it cached
                         std::vector<uint8_t> cached_data = feature_data[report_id];
                         uint8_t buf[256];
                         memset(buf, 0, sizeof(buf));
                         size_t copy_len = std::min((size_t)setup.wLength, cached_data.size() - 1);
                         memcpy(buf, cached_data.data() + 1, copy_len);

                         if (setup.wLength <= sizeof(buf)) {
                             write(ep0_fd, buf, setup.wLength);
                         } else {
                             write(ep0_fd, buf, sizeof(buf));
                         }
                     } else {
                         // Request from BT and defer EP0 write
                         feature_pending = true;
                         current_setup = setup;

                         // Trigger BT read
                         uint8_t get_feature[2] = {0x43, report_id};
                         bt_write(CONTROL, get_feature, sizeof(get_feature));
                     }
                 } else {
                     uint8_t buf[64];
                     memset(buf, 0, sizeof(buf));
                     write(ep0_fd, buf, setup.wLength);
                 }
            } else {
                read(ep0_fd, NULL, 0);
            }
        } else {
            read(ep0_fd, NULL, 0); // Stall
        }
    } else {
        // Host is writing to us
        if ((setup.bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS) {
            if (setup.bRequest == HID_REQ_SET_REPORT) {
                 uint8_t buf[256];
                 if (setup.wLength > 0 && setup.wLength <= sizeof(buf)) {
                     ret = read(ep0_fd, buf, setup.wLength);
                     if (ret > 0) {
                         uint8_t report_type = setup.wValue >> 8;
                         uint8_t report_id = setup.wValue & 0xFF;

                         if (report_type == 3) { // Feature Report
                             uint8_t final_buf[256];
                             final_buf[0] = 0x53; // 0x53 is SET_REPORT (Feature)
                             final_buf[1] = report_id;
                             memcpy(final_buf + 2, buf + 1, ret - 1);
                             // The total payload is `ret` bytes (including report ID at buf[0]).
                             // We are sending `0x53` (1 byte) + `report_id` (1 byte) + `buf + 1` (ret - 1 bytes) + `CRC` (4 bytes).
                             // So total size is `1 + 1 + ret - 1 + 4` = `ret + 5`.
                             // However, the checksum function expects data starting after `0x53`.
                             // We pass `final_buf + 1` to `fill_feature_report_checksum`, and its length should be `ret + 4` (ID + payload + CRC)
                             fill_feature_report_checksum(final_buf + 1, ret + 4);
                             bt_write(CONTROL, final_buf, ret + 5);
                         }
                     }
                 } else if (setup.wLength > 0) {
                     // Empty read to discard unhandled data if we didn't read it
                     uint8_t discard[256];
                     read(ep0_fd, discard, setup.wLength > sizeof(discard) ? sizeof(discard) : setup.wLength);
                 }
                 read(ep0_fd, NULL, 0); // Acknowledge status stage
            } else {
                 read(ep0_fd, NULL, 0);
            }
        } else {
             read(ep0_fd, NULL, 0);
        }
    }
}

void usb_process_pending_feature(uint8_t report_id) {
    if (feature_pending && (current_setup.wValue & 0xFF) == report_id) {
        std::vector<uint8_t> cached_data;
        if (feature_data.find(report_id) != feature_data.end()) {
            cached_data = feature_data[report_id];
        }

        uint8_t buf[256];
        memset(buf, 0, sizeof(buf));

        if (!cached_data.empty()) {
            size_t copy_len = std::min((size_t)current_setup.wLength, cached_data.size() - 1);
            memcpy(buf, cached_data.data() + 1, copy_len);
        } else {
            buf[0] = report_id;
        }

        if (current_setup.wLength <= sizeof(buf)) {
            write(ep0_fd, buf, current_setup.wLength);
        } else {
            write(ep0_fd, buf, sizeof(buf));
        }
        feature_pending = false;
    }
}
