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
int ep_hid_in_fd = -1;
int ep_hid_out_fd = -1;
bool usb_gadget_bound = false;
bool uac1_enabled = false;

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

/*
 * FunctionFS descriptors — HID ONLY
 *
 * Audio is now handled by the kernel's f_uac1 driver via ConfigFS.
 * FunctionFS only describes the HID interface + 2 endpoints:
 *   - Interface 0: HID (class 0x03), 2 endpoints
 *   - HID class descriptor (9 bytes)
 *   - EP1 IN  (0x81): Interrupt, 64 bytes — HID reports from controller
 *   - EP2 OUT (0x02): Interrupt, 64 bytes — HID output reports (rumble, LEDs)
 *
 * FunctionFS will remap interface numbers and endpoint addresses
 * when the gadget is bound to the UDC. We use sequential numbering.
 */

// HID-only descriptor data (32 bytes per speed)
// 4 descriptors: Interface + HID + EP IN + EP OUT
struct usb_ext_descriptors {
    struct usb_functionfs_descs_head_v2 header;
    __le32 fs_count;
    __le32 hs_count;

    // Full-speed descriptors
    // Interface Descriptor (9 bytes)
    struct usb_interface_descriptor fs_intf;
    // HID Descriptor (9 bytes)
    struct usb_hid_descriptor fs_hid;
    // Endpoint IN Descriptor (7 bytes)
    struct usb_endpoint_descriptor_no_audio fs_ep_in;
    // Endpoint OUT Descriptor (7 bytes)
    struct usb_endpoint_descriptor_no_audio fs_ep_out;

    // High-speed descriptors (same layout)
    struct usb_interface_descriptor hs_intf;
    struct usb_hid_descriptor hs_hid;
    struct usb_endpoint_descriptor_no_audio hs_ep_in;
    struct usb_endpoint_descriptor_no_audio hs_ep_out;
} __attribute__((packed));

static struct usb_ext_descriptors descriptors = {
    .header = {
        .magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
        .length = htole32(sizeof(descriptors)),
        .flags = htole32(FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC),
    },
    .fs_count = htole32(4), // Interface + HID + EP_IN + EP_OUT
    .hs_count = htole32(4),

    // === Full-Speed Descriptors ===
    .fs_intf = {
        .bLength            = sizeof(struct usb_interface_descriptor),  // 9
        .bDescriptorType    = USB_DT_INTERFACE,                        // 0x04
        .bInterfaceNumber   = 0,
        .bAlternateSetting  = 0,
        .bNumEndpoints      = 2,
        .bInterfaceClass    = USB_CLASS_HID,                           // 0x03
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface         = 0,
    },
    .fs_hid = {
        .bLength              = sizeof(struct usb_hid_descriptor),     // 9
        .bDescriptorType      = HID_DT_HID,                           // 0x21
        .bcdHID               = htole16(0x0111),                       // HID 1.11
        .bCountryCode         = 0,
        .bNumDescriptors      = 1,
        .bReportDescriptorType = HID_DT_REPORT,                       // 0x22
        .wDescriptorLength    = htole16(0),  // patched at runtime with desc_hid_report_ds_len
    },
    .fs_ep_in = {
        .bLength          = sizeof(struct usb_endpoint_descriptor_no_audio),  // 7
        .bDescriptorType  = USB_DT_ENDPOINT,                                  // 0x05
        .bEndpointAddress = 0x81,  // EP1 IN
        .bmAttributes     = USB_ENDPOINT_XFER_INT,                             // 0x03 Interrupt
        .wMaxPacketSize   = htole16(64),
        .bInterval        = 1,  // 1ms polling
    },
    .fs_ep_out = {
        .bLength          = sizeof(struct usb_endpoint_descriptor_no_audio),  // 7
        .bDescriptorType  = USB_DT_ENDPOINT,                                  // 0x05
        .bEndpointAddress = 0x02,  // EP2 OUT
        .bmAttributes     = USB_ENDPOINT_XFER_INT,                             // 0x03 Interrupt
        .wMaxPacketSize   = htole16(64),
        .bInterval        = 1,  // 1ms polling
    },

    // === High-Speed Descriptors (identical) ===
    .hs_intf = {
        .bLength            = sizeof(struct usb_interface_descriptor),
        .bDescriptorType    = USB_DT_INTERFACE,
        .bInterfaceNumber   = 0,
        .bAlternateSetting  = 0,
        .bNumEndpoints      = 2,
        .bInterfaceClass    = USB_CLASS_HID,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface         = 0,
    },
    .hs_hid = {
        .bLength              = sizeof(struct usb_hid_descriptor),
        .bDescriptorType      = HID_DT_HID,
        .bcdHID               = htole16(0x0111),
        .bCountryCode         = 0,
        .bNumDescriptors      = 1,
        .bReportDescriptorType = HID_DT_REPORT,
        .wDescriptorLength    = htole16(0),  // patched at runtime
    },
    .hs_ep_in = {
        .bLength          = sizeof(struct usb_endpoint_descriptor_no_audio),
        .bDescriptorType  = USB_DT_ENDPOINT,
        .bEndpointAddress = 0x81,  // EP1 IN
        .bmAttributes     = USB_ENDPOINT_XFER_INT,
        .wMaxPacketSize   = htole16(64),
        .bInterval        = 1,
    },
    .hs_ep_out = {
        .bLength          = sizeof(struct usb_endpoint_descriptor_no_audio),
        .bDescriptorType  = USB_DT_ENDPOINT,
        .bEndpointAddress = 0x02,  // EP2 OUT
        .bmAttributes     = USB_ENDPOINT_XFER_INT,
        .wMaxPacketSize   = htole16(64),
        .bInterval        = 1,
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

static bool try_bind_udc() {
    int ret = system("echo dummy_udc.0 > /sys/kernel/config/usb_gadget/dualsense/UDC 2>/dev/null");
    if (ret != 0) return false;

    // Verify binding by reading back the UDC file.
    // If it contains our UDC name, the kernel accepted the bind.
    // Note: checking /sys/class/udc/dummy_udc.0/state is unreliable —
    // dummy_hcd shows "not attached" even after a successful bind.
    FILE *f = fopen("/sys/kernel/config/usb_gadget/dualsense/UDC", "r");
    if (!f) return false;
    char udc_name[64] = {0};
    if (fgets(udc_name, sizeof(udc_name), f)) {
        fclose(f);
        // Strip trailing newline
        char *nl = strchr(udc_name, '\n');
        if (nl) *nl = '\0';
        if (strlen(udc_name) > 0) {
            printf("[USB] UDC file confirms bind: '%s'\n", udc_name);
            return true;
        }
    } else {
        fclose(f);
    }
    return false;
}

static bool check_uac1_flag() {
    FILE *f = fopen("/tmp/ds5_uac1_enabled", "r");
    if (!f) return false;
    char buf[4] = {0};
    if (fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return buf[0] == '1';
    }
    fclose(f);
    return false;
}

int usb_init() {
    printf("[USB] Running setup_gadget.sh...\n");
    if (system("./setup_gadget.sh") != 0 && system("../setup_gadget.sh") != 0) {
        printf("[USB] Failed to run setup_gadget.sh. Make sure you are root and the script is in the current or parent directory.\n");
        return -1;
    }

    // Check whether setup_gadget.sh enabled UAC1 (depends on UDC capabilities)
    uac1_enabled = check_uac1_flag();
    if (uac1_enabled) {
        printf("[USB] UAC1 audio is ENABLED (real UDC detected).\n");
    } else {
        printf("[USB] UAC1 audio is DISABLED (dummy_hcd — no isochronous support).\n");
    }

    // Patch HID report descriptor length into the descriptors
    descriptors.fs_hid.wDescriptorLength = htole16(desc_hid_report_ds_len);
    descriptors.hs_hid.wDescriptorLength = htole16(desc_hid_report_ds_len);

    printf("[USB] Opening FFS ep0...\n");
    ep0_fd = open("/dev/ffs/ep0", O_RDWR);
    if (ep0_fd < 0) {
        perror("[USB] Failed to open ep0");
        return -1;
    }

    printf("[USB] Writing descriptors (%lu bytes)...\n", sizeof(descriptors));
    ssize_t ret = write(ep0_fd, &descriptors, sizeof(descriptors));
    if (ret < 0) {
        perror("[USB] Failed to write descriptors");
        printf("[USB] errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }
    printf("[USB] Descriptors written successfully (%zd bytes).\n", ret);

    printf("[USB] Writing strings...\n");
    if (write(ep0_fd, &strings, sizeof(strings)) < 0) {
        perror("[USB] Failed to write strings");
        return -1;
    }

    printf("[USB] Descriptors and strings written. Opening endpoints...\n");

    // FunctionFS now only has HID endpoints:
    // ep1 = HID IN (reports from controller to host)
    // ep2 = HID OUT (output reports from host to controller)
    ep_hid_in_fd = open("/dev/ffs/ep1", O_RDWR | O_NONBLOCK);
    if (ep_hid_in_fd < 0) {
        perror("[USB] Failed to open ep1 (HID IN)");
        return -1;
    }

    ep_hid_out_fd = open("/dev/ffs/ep2", O_RDWR | O_NONBLOCK);
    if (ep_hid_out_fd < 0) {
        perror("[USB] Failed to open ep2 (HID OUT)");
        return -1;
    }

    // Give the kernel a moment to register the FFS function
    // after all endpoints have been opened
    usleep(100000); // 100ms

    // Attempt UDC binding with retries
    printf("[USB] Binding gadget to dummy UDC...\n");
    usb_gadget_bound = false;
    for (int attempt = 1; attempt <= 5; attempt++) {
        if (try_bind_udc()) {
            usb_gadget_bound = true;
            printf("[USB] Gadget successfully bound to UDC (attempt %d).\n", attempt);
            break;
        }
        printf("[USB] UDC bind attempt %d/5 failed, retrying in 500ms...\n", attempt);
        usleep(500000); // 500ms
    }

    if (!usb_gadget_bound) {
        printf("[USB] ERROR: Failed to bind UDC after 5 attempts.\n");
        printf("[USB] HID will not work. Audio will not be available.\n");
        printf("[USB] Try running: sudo ./teardown_gadget.sh && sudo ./setup_gadget.sh\n");
        return -1;
    }

    return 0;
}

void usb_deinit() {
    if (ep_hid_in_fd >= 0) { close(ep_hid_in_fd); ep_hid_in_fd = -1; }
    if (ep_hid_out_fd >= 0) { close(ep_hid_out_fd); ep_hid_out_fd = -1; }
    if (ep0_fd >= 0) { close(ep0_fd); ep0_fd = -1; }

    usb_gadget_bound = false;

    // Use teardown script for proper cleanup
    if (system("./teardown_gadget.sh 2>/dev/null") != 0) {
        system("../teardown_gadget.sh 2>/dev/null");
    }
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
