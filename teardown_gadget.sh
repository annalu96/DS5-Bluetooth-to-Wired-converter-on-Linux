#!/bin/bash
# teardown_gadget.sh — Forcefully clean up any stale USB gadget state
# Run this before setup_gadget.sh or on daemon shutdown.

set -e

GADGET_NAME="dualsense"
GADGET_DIR="/sys/kernel/config/usb_gadget/$GADGET_NAME"
MOUNT_DIR="/dev/ffs"
FFS_FUNC_NAME="ffs.ds"
UAC1_FUNC_NAME="uac1.usb0"

echo "[teardown] Starting full gadget cleanup..."

# 1. Unbind UDC (stop the gadget)
if [ -e "$GADGET_DIR/UDC" ] && [ -n "$(cat "$GADGET_DIR/UDC" 2>/dev/null)" ]; then
    echo "[teardown] Unbinding UDC..."
    echo "" > "$GADGET_DIR/UDC" 2>/dev/null || true
    sleep 0.5
fi

# 2. Unmount FunctionFS (releases ffs_data)
if mountpoint -q "$MOUNT_DIR" 2>/dev/null; then
    echo "[teardown] Unmounting FunctionFS at $MOUNT_DIR..."
    umount "$MOUNT_DIR" 2>/dev/null || umount -l "$MOUNT_DIR" 2>/dev/null || true
fi
# Also try in case mountpoint check missed it
umount "$MOUNT_DIR" 2>/dev/null || true

# 3. Wait for kernel to finish ffs_data_put() / ffs_fs_kill_sb
echo "[teardown] Waiting for kernel FFS cleanup..."
sleep 1

# 4. Remove function symlinks from config
if [ -L "$GADGET_DIR/configs/c.1/$FFS_FUNC_NAME" ]; then
    echo "[teardown] Unlinking $FFS_FUNC_NAME from config..."
    rm -f "$GADGET_DIR/configs/c.1/$FFS_FUNC_NAME"
fi
if [ -L "$GADGET_DIR/configs/c.1/$UAC1_FUNC_NAME" ]; then
    echo "[teardown] Unlinking $UAC1_FUNC_NAME from config..."
    rm -f "$GADGET_DIR/configs/c.1/$UAC1_FUNC_NAME"
fi

# 5. Remove config strings and config directory
rmdir "$GADGET_DIR/configs/c.1/strings/0x409" 2>/dev/null || true
rmdir "$GADGET_DIR/configs/c.1" 2>/dev/null || true

# 6. Remove function directories
rmdir "$GADGET_DIR/functions/$FFS_FUNC_NAME" 2>/dev/null || true
rmdir "$GADGET_DIR/functions/$UAC1_FUNC_NAME" 2>/dev/null || true

# 7. Remove gadget strings and gadget directory
rmdir "$GADGET_DIR/strings/0x409" 2>/dev/null || true
rmdir "$GADGET_DIR" 2>/dev/null || true

# 8. Force-reload modules to clear any zombie state
# Only if refcount allows (module is not in use)
echo "[teardown] Reloading kernel modules to clear stale state..."
modprobe -r usb_f_uac1 2>/dev/null || true
modprobe -r u_audio 2>/dev/null || true
modprobe -r usb_f_fs 2>/dev/null || true
# Don't unload libcomposite or dummy_hcd — they're shared
sleep 0.5

# Reload them fresh
modprobe usb_f_fs 2>/dev/null || true
modprobe usb_f_uac1 2>/dev/null || true

# 9. Verify cleanup
if [ -d "$GADGET_DIR" ]; then
    echo "[teardown] WARNING: Gadget directory still exists at $GADGET_DIR"
    echo "[teardown] Contents:"
    ls -la "$GADGET_DIR/" 2>/dev/null || true
else
    echo "[teardown] Gadget directory removed successfully."
fi

echo "[teardown] Cleanup complete."
