import re

with open('old_usb_conf_extracted.c', 'r') as f:
    old_conf = f.read()

start = old_conf.find('// --- INTERFACE DESCRIPTOR (0.0): Audio Control ---')
end = old_conf.find('};')
desc_str = old_conf[start:end]

lines = desc_str.split('\n')

byte_index = 0
for i, line in enumerate(lines):
    clean_line = line.strip()
    if not clean_line or clean_line.startswith('//'):
        continue
    hex_vals = re.findall(r'0x[0-9A-Fa-f]{2}', clean_line)

    if 'wDescriptorLength' in clean_line:
        print(f"wDescriptorLength found at byte index {byte_index} in line: {clean_line}")

    byte_index += len(hex_vals)
