#!/bin/bash
# test_hidraw_rumble.sh — Write rumble output report directly to gadget hidraw
# This tests the EXACT path Steam uses (hidraw write → USB → bridge → BT)
# Run as user (not root) to simulate Steam's access.

echo "[test] Looking for gadget DualSense hidraw (USB bus, 054C:0CE6)..."

GADGET_HIDRAW=""
for h in /sys/class/hidraw/hidraw*; do
    name=$(basename "$h")
    uevent="$h/device/uevent"
    [ -f "$uevent" ] || continue
    # Match USB bus (0003) + Sony DualSense
    if grep -q "^HID_ID=0003:0000054C:00000CE6" "$uevent" 2>/dev/null; then
        GADGET_HIDRAW="/dev/$name"
        hid_name=$(grep "^HID_NAME=" "$uevent" | cut -d= -f2)
        echo "[test] Found: $GADGET_HIDRAW ($hid_name)"
        break
    fi
done

if [ -z "$GADGET_HIDRAW" ]; then
    echo "[test] ❌ No USB DualSense hidraw found. Is the bridge/gadget running?"
    exit 1
fi

# Check permissions
if ! [ -r "$GADGET_HIDRAW" ] && ! [ -w "$GADGET_HIDRAW" ]; then
    echo "[test] ⚠️ Cannot access $GADGET_HIDRAW (permission denied)"
    echo "[test]    Running as: $(whoami)"
    echo "[test]    Perms: $(ls -la $GADGET_HIDRAW)"
    echo "[test]    Try: sudo $0"
    exit 1
fi

echo "[test] Writing USB output report 0x02 with rumble to $GADGET_HIDRAW..."
echo "[test] Path: hidraw write → kernel HID → USB transport → FunctionFS EP2 OUT → bridge"

# USB DualSense Output Report (ID 0x02):
# Byte 0: Report ID = 0x02
# Byte 1: valid_flag0 (0x03 = COMPATIBLE_VIBRATION | HAPTICS_SELECT)
# Byte 2: valid_flag1
# Byte 3: motor_right (weak)
# Byte 4: motor_left (strong)
# Bytes 5-47: rest of common payload (zeros)
# Total: 48 bytes

# Build: 0x02, 0x03, 0x00, 0xC0(right), 0xFF(left), then 43 zero bytes = 48 bytes
python3 -c "
import os, time

# USB Output Report ID 0x02 (48 bytes as per HID descriptor: report_id + 47 bytes)
report = bytearray(48)
report[0] = 0x02  # Report ID
report[1] = 0x03  # valid_flag0: COMPATIBLE_VIBRATION | HAPTICS_SELECT
report[2] = 0x00  # valid_flag1
report[3] = 0xC0  # motor_right (weak) = 192
report[4] = 0xFF  # motor_left (strong) = 255

fd = os.open('$GADGET_HIDRAW', os.O_RDWR)
print(f'[test] Opened {\"$GADGET_HIDRAW\"} (fd={fd})')
print(f'[test] Sending: report_id=0x02 flags=0x03|0x00 motor_r=0xC0 motor_l=0xFF')

try:
    written = os.write(fd, bytes(report))
    print(f'[test] ✅ Wrote {written} bytes — RUMBLE ON')
except OSError as e:
    print(f'[test] ❌ Write failed: {e}')
    os.close(fd)
    exit(1)

time.sleep(1.0)

# Stop rumble
report[3] = 0x00  # motor_right = 0
report[4] = 0x00  # motor_left = 0
try:
    written = os.write(fd, bytes(report))
    print(f'[test] ✅ Wrote {written} bytes — RUMBLE OFF')
except OSError as e:
    print(f'[test] ❌ Stop write failed: {e}')

os.close(fd)
print('[test] Done.')
"
