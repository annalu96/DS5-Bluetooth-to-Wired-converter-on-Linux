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
FALLBACK_EVENT=""

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
            
            # Read the uevent to check bus type (0003=USB, 0005=BT)
            bus_info=$(grep "ID=" "$uevent_file" 2>/dev/null | head -1)
            is_usb=false
            if echo "$bus_info" | grep -q "^ID=0003"; then
                is_usb=true
            fi
            
            echo "[test] Found: /dev/input/$ev_name — $name (${is_usb:+USB}${is_usb:-BT})"
            
            # Check if this device supports force feedback
            ff_file="$ev/device/capabilities/ff"
            if [ -f "$ff_file" ]; then
                ff_cap=$(cat "$ff_file" 2>/dev/null)
                if [ "$ff_cap" != "0" ] && [ -n "$ff_cap" ]; then
                    echo "[test]    Has FF capability: $ff_cap"
                    if [ "$is_usb" = true ]; then
                        # Prefer USB (virtual) device for testing the bridge
                        VIRTUAL_EVENT="/dev/input/$ev_name"
                    elif [ -z "$VIRTUAL_EVENT" ]; then
                        FALLBACK_EVENT="/dev/input/$ev_name"
                    fi
                fi
            fi
        fi
    fi
done

# Fall back to BT device if no USB device found
if [ -z "$VIRTUAL_EVENT" ] && [ -n "$FALLBACK_EVENT" ]; then
    echo "[test] ⚠️ No USB DualSense found, falling back to BT device"
    VIRTUAL_EVENT="$FALLBACK_EVENT"
fi

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
import ctypes, struct, fcntl, os, time

# Open the event device
fd = os.open('$VIRTUAL_EVENT', os.O_RDWR)

# Define the ff_effect struct using ctypes for correct layout
class ff_rumble_effect(ctypes.Structure):
    _fields_ = [('strong_magnitude', ctypes.c_uint16),
                ('weak_magnitude', ctypes.c_uint16)]

class ff_trigger(ctypes.Structure):
    _fields_ = [('button', ctypes.c_uint16),
                ('interval', ctypes.c_uint16)]

class ff_replay(ctypes.Structure):
    _fields_ = [('length', ctypes.c_uint16),
                ('delay', ctypes.c_uint16)]

class ff_effect_union(ctypes.Union):
    _fields_ = [('rumble', ff_rumble_effect),
                ('_pad', ctypes.c_uint8 * 28)]

class ff_effect(ctypes.Structure):
    _fields_ = [('type', ctypes.c_uint16),
                ('id', ctypes.c_int16),
                ('direction', ctypes.c_uint16),
                ('trigger', ff_trigger),
                ('replay', ff_replay),
                ('u', ff_effect_union)]

effect = ff_effect()
effect.type = 0x50  # FF_RUMBLE
effect.id = -1
effect.direction = 0
effect.trigger.button = 0
effect.trigger.interval = 0
effect.replay.length = 800  # 800ms
effect.replay.delay = 0
effect.u.rumble.strong_magnitude = 0xC000
effect.u.rumble.weak_magnitude = 0xC000

# EVIOCSFF
EVIOCSFF = 0x40304580
buf = bytes(effect)
result = fcntl.ioctl(fd, EVIOCSFF, buf)
effect2 = ff_effect.from_buffer_copy(result)
effect_id = effect2.id
print(f'[test] Uploaded FF effect id={effect_id}')

# Play: write EV_FF event
EV_FF = 0x15
event = struct.pack('qqHHi', 0, 0, EV_FF, effect_id, 1)
os.write(fd, event)
print('[test] ✅ Rumble ON — controller should vibrate now')
time.sleep(0.8)

# Stop
event = struct.pack('qqHHi', 0, 0, EV_FF, effect_id, 0)
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
