import re

with open('src/usb.cpp', 'r') as f:
    usb_cpp = f.read()

start = usb_cpp.find('struct {')
end = usb_cpp.find('} __attribute__((packed)) descriptors = {')
end = usb_cpp.find('};', end) + 2

with open('descriptors_struct_dump.txt', 'r') as f:
    new_struct = f.read()

usb_cpp = usb_cpp[:start] + new_struct + usb_cpp[end:]

# also replace usb_init dynamic lengths
usb_cpp = usb_cpp.replace('descriptors.hid_fs.wDescriptorLength = htole16(desc_hid_report_ds_len);', 'descriptors.fs_desc[217] = desc_hid_report_ds_len & 0xFF;\n    descriptors.fs_desc[218] = (desc_hid_report_ds_len >> 8) & 0xFF;')
usb_cpp = usb_cpp.replace('descriptors.hid_hs.wDescriptorLength = htole16(desc_hid_report_ds_len);', 'descriptors.hs_desc[217] = desc_hid_report_ds_len & 0xFF;\n    descriptors.hs_desc[218] = (desc_hid_report_ds_len >> 8) & 0xFF;')

with open('src/usb.cpp', 'w') as f:
    f.write(usb_cpp)
