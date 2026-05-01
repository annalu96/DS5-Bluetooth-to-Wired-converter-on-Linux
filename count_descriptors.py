import re

with open('old_usb_conf_extracted.c', 'r') as f:
    old_conf = f.read()

start = old_conf.find('// --- INTERFACE DESCRIPTOR (0.0): Audio Control ---')
end = old_conf.find('};')

desc_str = old_conf[start:end]

# Split by lines
lines = desc_str.split('\n')
desc_count = 0
current_len = 0
for line in lines:
    line = line.strip()
    if not line or line.startswith('//'):
        continue
    # Extract hex
    hex_vals = re.findall(r'0x[0-9A-Fa-f]{2}', line)
    if hex_vals:
        if current_len == 0:
            # First byte is usually length
            try:
                current_len = int(hex_vals[0], 16)
                desc_count += 1
            except:
                pass
        current_len -= len(hex_vals)
        if current_len <= 0:
            current_len = 0

print("Descriptor count:", desc_count)
