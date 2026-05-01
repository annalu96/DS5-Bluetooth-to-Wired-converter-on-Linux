import re

with open('src/usb.cpp', 'r') as f:
    content = f.read()

content = content.replace('int ep1_fd = -1;', 'int ep_audio_out_fd = -1;')
content = content.replace('int ep2_fd = -1;', 'int ep_audio_in_fd = -1;')
content = content.replace('int ep3_fd = -1;', 'int ep_hid_in_fd = -1;\nint ep_hid_out_fd = -1;')

content = content.replace('ep1_fd = open("/dev/ffs/ep1", O_RDWR | O_NONBLOCK);', 'ep_audio_out_fd = open("/dev/ffs/ep1", O_RDWR | O_NONBLOCK);')
content = content.replace('if (ep1_fd < 0) {', 'if (ep_audio_out_fd < 0) {')
content = content.replace('perror("[USB] Failed to open ep1");', 'perror("[USB] Failed to open ep1 (Audio OUT)");')

content = content.replace('ep2_fd = open("/dev/ffs/ep2", O_RDWR | O_NONBLOCK);', 'ep_audio_in_fd = open("/dev/ffs/ep2", O_RDWR | O_NONBLOCK);')
content = content.replace('if (ep2_fd < 0) {', 'if (ep_audio_in_fd < 0) {')
content = content.replace('perror("[USB] Failed to open ep2");', 'perror("[USB] Failed to open ep2 (Audio IN)");')

content = content.replace('// TODO Phase 5: ep3', '''
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
''')

content = content.replace('if (ep1_fd >= 0) close(ep1_fd);', 'if (ep_audio_out_fd >= 0) close(ep_audio_out_fd);')
content = content.replace('if (ep2_fd >= 0) close(ep2_fd);', '''if (ep_audio_in_fd >= 0) close(ep_audio_in_fd);
    if (ep_hid_in_fd >= 0) close(ep_hid_in_fd);
    if (ep_hid_out_fd >= 0) close(ep_hid_out_fd);''')

with open('src/usb.cpp', 'w') as f:
    f.write(content)
