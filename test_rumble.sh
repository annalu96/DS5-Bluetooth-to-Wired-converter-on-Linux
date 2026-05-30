#!/bin/bash
# test_rumble.sh — Trigger rumble on the virtual DualSense USB gadget
#
# This script finds the virtual DualSense evdev node (the one created by
# the hid-playstation driver for the USB gadget) and sends a brief
# force-feedback rumble effect to test the output relay pipeline.
#
# Requirements: fftest (from linuxconsole/joystick package)
#   sudo apt install joystick

set -e

echo "[test] Looking for virtual DualSense event device..."

# Find DualSense evdev nodes
# The virtual one should be on bus USB (BUS_USB=3), while the real one
# is on bus Bluetooth (BUS_BLUETOOTH=5)
VIRTUAL_EVENT=""

for ev in /sys/class/input/event*; do
    name_file="$ev/device/name"
    [ -f "$name_file" ] || continue

    name=$(cat "$name_file" 2>/dev/null)
    
    # Check if it's a DualSense
    if echo "$name" | grep -qi "DualSense\|Wireless Controller"; then
        # Check uevent for bus type
        uevent_file="$ev/device/uevent"
        if [ -f "$uevent_file" ]; then
            # Get the basename (event number)
            ev_name=$(basename "$ev")
            
            # Read the uevent to check bus type
            bus_info=$(grep "ID=" "$uevent_file" 2>/dev/null | head -1)
            
            echo "[test] Found: /dev/input/$ev_name — $name"
            
            # Check if this device supports force feedback
            ff_file="$ev/device/capabilities/ff"
            if [ -f "$ff_file" ]; then
                ff_cap=$(cat "$ff_file" 2>/dev/null)
                if [ "$ff_cap" != "0" ] && [ -n "$ff_cap" ]; then
                    echo "[test]    Has FF capability: $ff_cap"
                    VIRTUAL_EVENT="/dev/input/$ev_name"
                fi
            fi
        fi
    fi
done

if [ -z "$VIRTUAL_EVENT" ]; then
    echo "[test] ❌ No DualSense event device with FF capability found."
    echo "[test]    Make sure the bridge is running and the gadget is enumerated."
    exit 1
fi

echo ""
echo "[test] 🎮 Using: $VIRTUAL_EVENT"
echo ""

# Check if fftest is available
if ! command -v fftest &>/dev/null; then
    echo "[test] ⚠️ fftest not found. Install with: sudo apt install joystick"
    echo "[test] Attempting manual FF effect via Python instead..."
    
    # Fallback: use Python to send FF effect
    python3 -c "
import struct, fcntl, os, time

# Open the event device
fd = os.open('$VIRTUAL_EVENT', os.O_RDWR)

# Upload a rumble effect
# struct ff_effect for FF_RUMBLE
# See linux/input.h
FF_RUMBLE = 0x50
ff_effect = struct.pack(
    'HhHHHHHHHHH6x',  # type, id, direction, trigger(btn,interval), replay(len,delay), strong, weak + padding
    FF_RUMBLE,  # type
    -1,         # id (-1 = auto-assign)
    0,          # direction
    0, 0,       # trigger button, trigger interval
    500, 0,     # replay length (ms), replay delay
    0xC000,     # strong magnitude
    0xC000,     # weak magnitude
)

# EVIOCSFF = 0x40304580
EVIOCSFF = 0x40304580
result = fcntl.ioctl(fd, EVIOCSFF, ff_effect)
effect_id = struct.unpack_from('h', result, 2)[0]
print(f'[test] Uploaded FF effect id={effect_id}')

# Play the effect: write EV_FF event
# struct input_event: time(16 bytes) + type(2) + code(2) + value(4)
EV_FF = 0x15
event = struct.pack('llHHi', 0, 0, EV_FF, effect_id, 1)  # value=1 = play
os.write(fd, event)
print('[test] ✅ Rumble ON — controller should vibrate now')
time.sleep(0.8)

# Stop
event = struct.pack('llHHi', 0, 0, EV_FF, effect_id, 0)
os.write(fd, event)
print('[test] ✅ Rumble OFF')

os.close(fd)
print('[test] Done.')
" 2>&1
    exit $?
fi

echo "[test] Running fftest on $VIRTUAL_EVENT..."
echo "[test] Select effect #1 (Periodic sinusoidal) or #0 (Rumble) and play it."
echo ""
fftest "$VIRTUAL_EVENT"
