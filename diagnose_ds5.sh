#!/bin/bash
# diagnose_ds5.sh — Collect DualSense bridge diagnostic info
# Run as root: sudo ./diagnose_ds5.sh

echo "╔══════════════════════════════════════════════╗"
echo "║  DualSense Bridge Diagnostic                 ║"
echo "╚══════════════════════════════════════════════╝"
echo ""

echo "=== 1. HID Devices (DualSense only) ==="
for h in /sys/class/hidraw/hidraw*; do
    name=$(basename "$h")
    uevent="$h/device/uevent"
    [ -f "$uevent" ] || continue
    if grep -q "054C" "$uevent" 2>/dev/null; then
        bus=$(grep "^HID_ID=" "$uevent" | cut -d: -f1 | cut -d= -f2)
        hid_name=$(grep "^HID_NAME=" "$uevent" | cut -d= -f2)
        driver=$(grep "^DRIVER=" "$uevent" | cut -d= -f2)
        bus_label="???"
        [ "$bus" = "0003" ] && bus_label="USB"
        [ "$bus" = "0005" ] && bus_label="BT"
        echo "  /dev/$name [$bus_label] driver=$driver"
        echo "    HID_NAME: $hid_name"
        echo "    Perms: $(ls -la /dev/$name 2>/dev/null | awk '{print $1, $3, $4}')"
        acl_user=$(getfacl -p "/dev/$name" 2>/dev/null | grep "^user:" | grep -v "^user::")
        [ -n "$acl_user" ] && echo "    ACL: $acl_user"
        users=$(fuser "/dev/$name" 2>/dev/null)
        if [ -n "$users" ]; then
            for pid in $users; do
                pname=$(ps -p "$pid" -o comm= 2>/dev/null)
                echo "    Open by: PID $pid ($pname)"
            done
        else
            echo "    Open by: (none)"
        fi
        echo ""
    fi
done

echo "=== 2. Evdev Devices (DualSense only) ==="
for dev in /sys/class/input/event*; do
    [ -f "$dev/device/name" ] || continue
    name=$(cat "$dev/device/name")
    echo "$name" | grep -qi "DualSense\|Wireless Controller" || continue
    real=$(readlink -f "$dev")
    ev_name=$(basename "$dev")
    bus="???"
    echo "$real" | grep -q "0005:054C:" && bus="BT"
    echo "$real" | grep -q "0003:054C:" && bus="USB"
    perms=$(ls -la "/dev/input/$ev_name" 2>/dev/null | awk '{print $1}')
    echo "  /dev/input/$ev_name [$bus] perms=$perms"
    echo "    Name: $name"
done
echo ""

echo "=== 3. USB Gadget ==="
if [ -d /sys/kernel/config/usb_gadget/dualsense ]; then
    echo "  Status: ACTIVE"
    echo "  Product: $(cat /sys/kernel/config/usb_gadget/dualsense/strings/0x409/product 2>/dev/null)"
    echo "  Manufacturer: $(cat /sys/kernel/config/usb_gadget/dualsense/strings/0x409/manufacturer 2>/dev/null)"
    echo "  UDC: $(cat /sys/kernel/config/usb_gadget/dualsense/UDC 2>/dev/null)"
else
    echo "  Status: NOT RUNNING"
fi
echo ""

echo "=== 4. Steam Controller FDs ==="
steam_pid=$(pgrep -x steam 2>/dev/null)
if [ -n "$steam_pid" ]; then
    echo "  Steam PID: $steam_pid"
    hidraw_fds=$(ls -la /proc/$steam_pid/fd 2>/dev/null | grep hidraw)
    if [ -n "$hidraw_fds" ]; then
        echo "$hidraw_fds" | while read line; do
            echo "  $line"
        done
    else
        echo "  No hidraw FDs open"
    fi
else
    echo "  Steam not running"
fi
echo ""

echo "=== 5. Bridge Process ==="
bridge_pid=$(pgrep -f dualsense_bridge 2>/dev/null | head -1)
if [ -n "$bridge_pid" ]; then
    echo "  Bridge PID: $bridge_pid"
    ls -la /proc/$bridge_pid/fd 2>/dev/null | grep hidraw | while read line; do
        echo "  $line"
    done
else
    echo "  Bridge not running"
fi
echo ""

echo "=== 6. SDL Joystick Test ==="
sdl2-jstest --list 2>/dev/null || echo "  sdl2-jstest not available"
echo ""
echo "Done."
