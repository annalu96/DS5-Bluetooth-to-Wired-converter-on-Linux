import re

with open('old_usb_conf_extracted.c', 'r') as f:
    old_conf = f.read()

start = old_conf.find('// --- INTERFACE DESCRIPTOR (0.0): Audio Control ---')
end = old_conf.find('};')
desc_str = old_conf[start:end]

lines = desc_str.split('\n')
byte_index = 0
found = False

for line in lines:
    line = line.strip()
    if not line or line.startswith('//'):
        continue
    hex_vals = re.findall(r'0x[0-9A-Fa-f]{2}', line)

    if 'wDescriptorLength' in line:
        print(f"wDescriptorLength found at byte index {byte_index}")
        found = True

    byte_index += len(hex_vals)
