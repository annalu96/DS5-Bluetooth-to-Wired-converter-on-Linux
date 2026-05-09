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
#include <poll.h>
#include <chrono>

uint8_t mute[2] = {0}; // 0: SPEAKER(0x02) 1: MIC(0x05)
float volume[2] = {1.0f, 1.0f}; // 0: SPEAKER(0x02) 1: MIC(0x05)

int ep0_fd = -1;
int ep_hid_in_fd = -1;
int ep_hid_out_fd = -1;
bool usb_gadget_bound = false;
bool uac1_enabled = false;


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
    printf("[USB] Expecting setup_gadget.sh to have been run already.\n");
    // if (system("./setup_gadget.sh") != 0 && system("../setup_gadget.sh") != 0) {
    //     printf("[USB] Failed to run setup_gadget.sh. Make sure you are root and the script is in the current or parent directory.\n");
    //     return -1;
    // }

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

    // ─── Critical: drain EP0 events during enumeration ───
    // After UDC bind, the host-side hid-playstation driver probes the device.
    // The probe sequence is:
    //   1. ENABLE, SET_IDLE, GET_DESCRIPTOR (immediate)
    //   2. GET_REPORT for feature reports 0x09, 0x20, 0x05 (after HID device creation, ~5-10s)
    // If we don't answer these in time, the driver probe fails with -ETIMEDOUT (-110).
    // We service EP0 for up to 15s, exiting early after 2s of silence.
    printf("[USB] Servicing EP0 during host enumeration (up to 15s)...\n");
    {
        struct pollfd pfd;
        pfd.fd = ep0_fd;
        pfd.events = POLLIN;

        auto start = std::chrono::steady_clock::now();
        const int max_ms = 15000;    // Total max wait
        const int idle_ms = 2000;    // Exit after 2s of no events
        auto last_event = start;

        while (true) {
            auto now = std::chrono::steady_clock::now();
            int total_elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            int idle_elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - last_event).count();

            if (total_elapsed >= max_ms) {
                printf("[USB] EP0 servicing: max timeout reached (%dms).\n", total_elapsed);
                break;
            }
            if (idle_elapsed >= idle_ms && total_elapsed > 5000) {
                // Only allow idle exit after at least 5s (to ensure we've seen the probe)
                printf("[USB] EP0 servicing: idle timeout after %dms total.\n", total_elapsed);
                break;
            }

            int wait_ms = std::min(max_ms - total_elapsed, idle_ms - idle_elapsed);
            if (wait_ms <= 0) wait_ms = 100;

            int pr = poll(&pfd, 1, wait_ms);
            if (pr > 0 && (pfd.revents & POLLIN)) {
                usb_handle_ep0();
                last_event = std::chrono::steady_clock::now();
            }
        }
        printf("[USB] EP0 enumeration servicing complete.\n");
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
    // FunctionFS can batch multiple events in one read().
    // Read up to 8 events at a time to avoid missing SETUP requests.
    struct usb_functionfs_event events[8];
    int ret = read(ep0_fd, events, sizeof(events));
    if (ret < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[USB] ep0 read error");
        return;
    }

    if (ret == 0) {
        printf("[USB] ep0: got 0 bytes (closed?)\n");
        return;
    }

    int num_events = ret / sizeof(struct usb_functionfs_event);
    if (ret % sizeof(struct usb_functionfs_event) != 0) {
        printf("[USB] ep0: unexpected read size %d (not multiple of %lu)\n",
               ret, sizeof(struct usb_functionfs_event));
        // Debug: print raw bytes
        uint8_t *raw = (uint8_t *)events;
        printf("[USB] ep0 raw: ");
        for (int i = 0; i < ret && i < 64; i++) printf("%02x ", raw[i]);
        printf("\n");
        return;
    }

    for (int ev_idx = 0; ev_idx < num_events; ev_idx++) {
    struct usb_functionfs_event &event = events[ev_idx];

    // Handle FunctionFS lifecycle events
    switch (event.type) {
    case FUNCTIONFS_BIND:
        printf("[USB] EP0 event: BIND\n");
        continue;
    case FUNCTIONFS_UNBIND:
        printf("[USB] EP0 event: UNBIND\n");
        continue;
    case FUNCTIONFS_ENABLE:
        printf("[USB] EP0 event: ENABLE (host configured the gadget)\n");
        continue;
    case FUNCTIONFS_DISABLE:
        printf("[USB] EP0 event: DISABLE\n");
        continue;
    case FUNCTIONFS_SUSPEND:
        printf("[USB] EP0 event: SUSPEND\n");
        continue;
    case FUNCTIONFS_RESUME:
        printf("[USB] EP0 event: RESUME\n");
        continue;
    case FUNCTIONFS_SETUP:
        // Process setup request below
        break;
    default:
        printf("[USB] EP0 unknown event type: %d\n", event.type);
        continue;
    }

    // Extract the USB control request from the FFS event
    struct usb_ctrlrequest setup = event.u.setup;

    if (setup.bRequestType & USB_DIR_IN) {
        // Host is reading from us
        if ((setup.bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
            if (setup.bRequest == USB_REQ_GET_DESCRIPTOR) {
                if ((setup.wValue >> 8) == HID_DT_REPORT) {
                    printf("[USB] Sending HID Report Descriptor (%u bytes)...\n", desc_hid_report_ds_len);
                    write(ep0_fd, desc_hid_report_ds, desc_hid_report_ds_len);
                } else if ((setup.wValue >> 8) == HID_DT_HID) {
                    // Host is requesting the 9-byte HID class descriptor
                    printf("[USB] Sending HID Class Descriptor (9 bytes)...\n");
                    struct usb_hid_descriptor hid_desc = descriptors.hs_hid;
                    hid_desc.wDescriptorLength = htole16(desc_hid_report_ds_len);
                    write(ep0_fd, &hid_desc, sizeof(hid_desc));
                } else {
                    // Stall other descriptor requests we don't handle
                    // FunctionFS handles most standard requests internally anyway
                    // This is for ones explicitly forwarded to user space
                    printf("[USB] STALL: GET_DESCRIPTOR type=0x%02x\n", setup.wValue >> 8);
                    read(ep0_fd, NULL, 0);
                }
            } else {
                printf("[USB] STALL: Standard IN request bRequest=0x%02x\n", setup.bRequest);
                read(ep0_fd, NULL, 0);
            }
        } else if ((setup.bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS) {
            if (setup.bRequest == HID_REQ_GET_REPORT) {
                 uint8_t report_type = setup.wValue >> 8;
                 uint8_t report_id = setup.wValue & 0xFF;
                 printf("[USB] GET_REPORT type=%u id=0x%02x len=%u\n", report_type, report_id, setup.wLength);

                 if (report_type == 3) { // Feature Report
                     // Use hidraw ioctl to get the feature report synchronously
                     uint8_t buf[256];
                     memset(buf, 0, sizeof(buf));
                     int fr_ret = bt_get_feature_report(report_id, buf, sizeof(buf));
                     if (fr_ret > 0) {
                         printf("[USB]    Feature report 0x%02x: got %d bytes from BT, host wants %u bytes\n",
                                report_id, fr_ret, setup.wLength);

                         if (report_id == 0x09 && fr_ret >= 7) {
                             // Spoof MAC address to prevent kernel -EEXIST (Duplicate device)
                             // since the BT connection is already using the real MAC.
                             buf[6] ^= 0x01;
                         }

                         // Always send exactly wLength bytes to the host.
                         // If BT returned fewer bytes, the buffer is already zero-padded.
                         // If BT returned more bytes, truncate to wLength.
                         size_t send_len = setup.wLength;
                         if (send_len > sizeof(buf)) send_len = sizeof(buf);
                         ssize_t wr = write(ep0_fd, buf, send_len);
                         if (wr < 0) {
                             printf("[USB] ❌ Failed to send feature report 0x%02x: %s\n",
                                    report_id, strerror(errno));
                         }
                     } else {
                         // Failed to get from controller — send zeros
                         printf("[USB] ⚠️ Feature report 0x%02x not available from controller\n", report_id);
                         memset(buf, 0, sizeof(buf));
                         buf[0] = report_id;
                         write(ep0_fd, buf, setup.wLength);
                     }
                 } else {
                     printf("[USB] ⚠️ Unhandled GET_REPORT type=%u for report 0x%02x\n", report_type, report_id);
                     uint8_t buf[64];
                     memset(buf, 0, sizeof(buf));
                     write(ep0_fd, buf, setup.wLength);
                 }
            } else if (setup.bRequest == HID_REQ_GET_IDLE) {
                printf("[USB] GET_IDLE\n");
                uint8_t idle_rate = 0; // Indefinite
                write(ep0_fd, &idle_rate, 1);
            } else if (setup.bRequest == HID_REQ_GET_PROTOCOL) {
                printf("[USB] GET_PROTOCOL\n");
                uint8_t protocol = 1; // Report protocol
                write(ep0_fd, &protocol, 1);
            } else {
                printf("[USB] STALL: Class IN request bRequest=0x%02x\n", setup.bRequest);
                read(ep0_fd, NULL, 0);
            }
        } else {
            read(ep0_fd, NULL, 0); // Stall
        }
    } else {
        // Host is writing to us (OUT direction)
        if ((setup.bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS) {
            if (setup.bRequest == HID_REQ_SET_REPORT) {
                 uint8_t buf[256];
                 if (setup.wLength > 0 && setup.wLength <= sizeof(buf)) {
                     ret = read(ep0_fd, buf, setup.wLength);
                     if (ret > 0) {
                         uint8_t report_type = setup.wValue >> 8;
                         uint8_t report_id = setup.wValue & 0xFF;
                         printf("[USB] SET_REPORT type=%u id=0x%02x len=%d\n", report_type, report_id, ret);

                         if (report_type == 3) { // Feature Report
                             // Use hidraw ioctl to set the feature report directly
                             // hidraw expects: [report_id][data...]
                             bt_set_feature_report(buf, ret);
                         }
                     }
                 } else if (setup.wLength > 0) {
                     // Empty read to discard unhandled data if we didn't read it
                     uint8_t discard[256];
                     read(ep0_fd, discard, setup.wLength > sizeof(discard) ? sizeof(discard) : setup.wLength);
                 }
                 // Note: FunctionFS handles status stage automatically for OUT requests
            } else if (setup.bRequest == HID_REQ_SET_IDLE) {
                printf("[USB] SET_IDLE value=0x%04x\n", setup.wValue);
                // Acknowledge — FunctionFS handles status stage for OUT with wLength==0
                // Just need to not stall
                if (setup.wLength == 0) {
                    read(ep0_fd, NULL, 0); // Acknowledge status stage
                }
            } else if (setup.bRequest == HID_REQ_SET_PROTOCOL) {
                printf("[USB] SET_PROTOCOL value=0x%04x\n", setup.wValue);
                if (setup.wLength == 0) {
                    read(ep0_fd, NULL, 0); // Acknowledge status stage
                }
            } else {
                printf("[USB] STALL: Class OUT request bRequest=0x%02x\n", setup.bRequest);
                read(ep0_fd, NULL, 0);
            }
        } else {
            printf("[USB] STALL: Non-class OUT request type=0x%02x req=0x%02x\n",
                   setup.bRequestType, setup.bRequest);
            read(ep0_fd, NULL, 0);
        }
    }
    } // end for each event
}

void usb_process_pending_feature(uint8_t report_id) {
    // No-op: feature reports are now handled synchronously via hidraw ioctl.
    // This function is kept for API compatibility but does nothing.
    (void)report_id;
}
