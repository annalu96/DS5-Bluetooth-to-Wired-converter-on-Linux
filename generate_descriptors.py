import re

with open('old_usb_conf_extracted.c', 'r') as f:
    old_conf = f.read()

start = old_conf.find('// --- INTERFACE DESCRIPTOR (0.0): Audio Control ---')
end = old_conf.find('};')
desc_str = old_conf[start:end]

lines = desc_str.split('\n')

hex_array = []
for line in lines:
    if '#ifdef ENABLE_DSE' in line or '#else' in line or '#endif' in line:
        continue
    clean_line = line.strip()
    if not clean_line or clean_line.startswith('//'):
        continue
    if 'wDescriptorLength: 405' in clean_line:
        continue
    hex_vals = re.findall(r'0x[0-9A-Fa-f]{2}', clean_line)
    hex_array.extend(hex_vals)

assert len(hex_array) == 234

# Format the hex array into lines
formatted_array = ""
for i in range(0, len(hex_array), 8):
    formatted_array += "        " + ", ".join(hex_array[i:i+8]) + ",\n"

cpp_struct = f"""struct usb_ext_descriptors {{
    struct usb_functionfs_descs_head_v2 header;
    __le32 fs_count;
    __le32 hs_count;

    uint8_t fs_desc[234];
    uint8_t hs_desc[234];
}} __attribute__((packed));

struct usb_ext_descriptors descriptors = {{
    .header = {{
        .magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
        .length = htole32(sizeof(descriptors)),
        .flags = htole32(FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC),
    }},
    .fs_count = htole32(26),
    .hs_count = htole32(26),
    .fs_desc = {{
{formatted_array}
    }},
    .hs_desc = {{
{formatted_array}
    }}
}};
"""

print(cpp_struct)
with open('descriptors_struct_dump.txt', 'w') as f:
    f.write(cpp_struct)
