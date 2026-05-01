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
#include "bt.h"
#include "utils.h"

uint8_t mute[2] = {0}; // 0: SPEAKER(0x02) 1: MIC(0x05)
float volume[2] = {1.0f, 1.0f}; // 0: SPEAKER(0x02) 1: MIC(0x05)

int ep0_fd = -1;
int ep1_fd = -1;
int ep2_fd = -1;
int ep3_fd = -1;

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

struct {
    struct usb_functionfs_descs_head_v2 header;
    __le32 fs_count;
    __le32 hs_count;

    // FS Descriptors
    struct usb_interface_descriptor intf_fs;
    struct usb_hid_descriptor hid_fs;
    struct usb_endpoint_descriptor_no_audio ep1_fs; // IN
    struct usb_endpoint_descriptor_no_audio ep2_fs; // OUT

    // HS Descriptors (same as FS for this simple case)
    struct usb_interface_descriptor intf_hs;
    struct usb_hid_descriptor hid_hs;
    struct usb_endpoint_descriptor_no_audio ep1_hs; // IN
    struct usb_endpoint_descriptor_no_audio ep2_hs; // OUT

} __attribute__((packed)) descriptors = {
    .header = {
        .magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
        .length = htole32(sizeof(descriptors)),
        .flags = htole32(FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC),
    },
    .fs_count = htole32(4),
    .hs_count = htole32(4),

    // --- FS ---
    .intf_fs = {
        .bLength = sizeof(struct usb_interface_descriptor),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bAlternateSetting = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = USB_CLASS_HID,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface = 1,
    },
    .hid_fs = {
        .bLength = sizeof(struct usb_hid_descriptor),
        .bDescriptorType = HID_DT_HID,
        .bcdHID = htole16(0x0111),
        .bCountryCode = 0,
        .bNumDescriptors = 1,
        .bReportDescriptorType = HID_DT_REPORT,
        .wDescriptorLength = htole16(0), // Set dynamically
    },
    .ep1_fs = {
        .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 1 | USB_DIR_IN,
        .bmAttributes = USB_ENDPOINT_XFER_INT,
        .wMaxPacketSize = htole16(64),
        .bInterval = 1,
    },
    .ep2_fs = {
        .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 2 | USB_DIR_OUT,
        .bmAttributes = USB_ENDPOINT_XFER_INT,
        .wMaxPacketSize = htole16(64),
        .bInterval = 1,
    },

    // --- HS ---
    .intf_hs = {
        .bLength = sizeof(struct usb_interface_descriptor),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bAlternateSetting = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = USB_CLASS_HID,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface = 1,
    },
    .hid_hs = {
        .bLength = sizeof(struct usb_hid_descriptor),
        .bDescriptorType = HID_DT_HID,
        .bcdHID = htole16(0x0111),
        .bCountryCode = 0,
        .bNumDescriptors = 1,
        .bReportDescriptorType = HID_DT_REPORT,
        .wDescriptorLength = htole16(0), // Set dynamically
    },
    .ep1_hs = {
        .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 1 | USB_DIR_IN,
        .bmAttributes = USB_ENDPOINT_XFER_INT,
        .wMaxPacketSize = htole16(64),
        .bInterval = 1,
    },
    .ep2_hs = {
        .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 2 | USB_DIR_OUT,
        .bmAttributes = USB_ENDPOINT_XFER_INT,
        .wMaxPacketSize = htole16(64),
        .bInterval = 1,
    },
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
    if (system("./setup_gadget.sh") != 0) {
        printf("[USB] Failed to run setup_gadget.sh. Make sure you are root.\n");
        // return -1;
    }

    // Since we dynamically initialized desc_hid_report_ds_len, we need to set wDescriptorLength
    descriptors.hid_fs.wDescriptorLength = htole16(desc_hid_report_ds_len);
    descriptors.hid_hs.wDescriptorLength = htole16(desc_hid_report_ds_len);

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

    ep1_fd = open("/dev/ffs/ep1", O_RDWR | O_NONBLOCK);
    if (ep1_fd < 0) {
        perror("[USB] Failed to open ep1");
        return -1;
    }

    ep2_fd = open("/dev/ffs/ep2", O_RDWR | O_NONBLOCK);
    if (ep2_fd < 0) {
        perror("[USB] Failed to open ep2");
        return -1;
    }

    // TODO Phase 5: ep3

    printf("[USB] Binding gadget to dummy UDC...\n");
    if (system("echo dummy_udc.0 > /sys/kernel/config/usb_gadget/dualsense/UDC") != 0) {
        printf("[USB] Failed to bind UDC.\n");
    } else {
        printf("[USB] Gadget successfully bound.\n");
    }

    return 0;
}

void usb_deinit() {
    if (ep1_fd >= 0) close(ep1_fd);
    if (ep2_fd >= 0) close(ep2_fd);
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
        // perror("[USB] ep0 read error");
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
                     uint8_t buf[64];
                     memset(buf, 0, sizeof(buf));
                     buf[0] = report_id;

                     if (setup.wLength <= sizeof(buf)) {
                         write(ep0_fd, buf, setup.wLength);

                         uint8_t get_feature[2] = {0x43, report_id};
                         bt_write(CONTROL, get_feature, sizeof(get_feature));
                     } else {
                         write(ep0_fd, buf, sizeof(buf));
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
                             fill_feature_report_checksum(final_buf + 1, ret);
                             bt_write(CONTROL, final_buf, ret + 1);
                         }
                     }
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
