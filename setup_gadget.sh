#!/bin/bash
# setup_gadget.sh — Create USB Gadget (DualSense HID + UAC1 Audio)
#
# This script:
# 1. Runs teardown to clean any stale state
# 2. Loads required kernel modules
# 3. Creates the composite gadget via ConfigFS
# 4. Mounts FunctionFS for HID endpoint access
#
# The UDC binding is NOT done here — it must be done from usb.cpp
# AFTER writing FFS descriptors and opening endpoints.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GADGET_NAME="dualsense"
GADGET_DIR="/sys/kernel/config/usb_gadget/$GADGET_NAME"
MOUNT_DIR="/dev/ffs"
FFS_FUNC_NAME="ffs.ds"
UAC1_FUNC_NAME="uac1.usb0"

# ============================================================
# Step 0: Full teardown of any previous state
# ============================================================
echo "[setup] Running teardown first..."
bash "$SCRIPT_DIR/teardown_gadget.sh"

# ============================================================
# Step 1: Load required kernel modules
# ============================================================
echo "[setup] Loading kernel modules..."
modprobe dummy_hcd 2>/dev/null || true
modprobe libcomposite 2>/dev/null || true
modprobe usb_f_fs 2>/dev/null || true
modprobe usb_f_uac1 2>/dev/null || true

# Wait for modules to settle
sleep 0.3

# Verify dummy_udc exists
if [ ! -d "/sys/class/udc/dummy_udc.0" ]; then
    echo "[setup] ERROR: dummy_udc.0 not found. Is dummy_hcd loaded?"
    exit 1
fi

# Verify ConfigFS is mounted
if [ ! -d "/sys/kernel/config/usb_gadget" ]; then
    echo "[setup] Mounting ConfigFS..."
    mount -t configfs none /sys/kernel/config 2>/dev/null || true
fi

# ============================================================
# Step 2: Create gadget directory and set device identity
# ============================================================
echo "[setup] Creating gadget '$GADGET_NAME'..."
mkdir -p "$GADGET_DIR"

# Set Device IDs (Sony DualSense)
echo "0x054c" > "$GADGET_DIR/idVendor"
echo "0x0ce6" > "$GADGET_DIR/idProduct"
echo "0x0100" > "$GADGET_DIR/bcdDevice" # 1.0.0
echo "0x0200" > "$GADGET_DIR/bcdUSB"    # USB 2.0

# Set Strings
mkdir -p "$GADGET_DIR/strings/0x409"
echo "Sony Interactive Entertainment" > "$GADGET_DIR/strings/0x409/manufacturer"
echo "DualSense Wireless Controller" > "$GADGET_DIR/strings/0x409/product"
echo "000000000000" > "$GADGET_DIR/strings/0x409/serialnumber"

# ============================================================
# Step 3: Create Config
# ============================================================
mkdir -p "$GADGET_DIR/configs/c.1"
echo 250 > "$GADGET_DIR/configs/c.1/MaxPower" # 500mA
mkdir -p "$GADGET_DIR/configs/c.1/strings/0x409"
echo "Config 1" > "$GADGET_DIR/configs/c.1/strings/0x409/configuration"

# ============================================================
# Step 4: Create UAC1 Audio Function (kernel-managed descriptors)
# DualSense audio: Speaker 4ch 48kHz 16-bit, Mic 2ch 48kHz 16-bit
# ============================================================
echo "[setup] Creating UAC1 audio function..."
mkdir -p "$GADGET_DIR/functions/$UAC1_FUNC_NAME"

# Playback (host -> gadget -> speaker): 4 channels, 48kHz, 16-bit
echo 0x0f > "$GADGET_DIR/functions/$UAC1_FUNC_NAME/p_chmask"  # 4 channels (bits 0-3)
echo 48000 > "$GADGET_DIR/functions/$UAC1_FUNC_NAME/p_srate"
echo 2 > "$GADGET_DIR/functions/$UAC1_FUNC_NAME/p_ssize"       # 2 bytes = 16-bit
echo 1 > "$GADGET_DIR/functions/$UAC1_FUNC_NAME/p_mute_present"
echo 1 > "$GADGET_DIR/functions/$UAC1_FUNC_NAME/p_volume_present"

# Capture (mic -> gadget -> host): 2 channels, 48kHz, 16-bit
echo 0x03 > "$GADGET_DIR/functions/$UAC1_FUNC_NAME/c_chmask"  # 2 channels (bits 0-1)
echo 48000 > "$GADGET_DIR/functions/$UAC1_FUNC_NAME/c_srate"
echo 2 > "$GADGET_DIR/functions/$UAC1_FUNC_NAME/c_ssize"       # 2 bytes = 16-bit
echo 1 > "$GADGET_DIR/functions/$UAC1_FUNC_NAME/c_mute_present"
echo 1 > "$GADGET_DIR/functions/$UAC1_FUNC_NAME/c_volume_present"

# Link UAC1 Function to Config (must be linked BEFORE FFS)
ln -s "$GADGET_DIR/functions/$UAC1_FUNC_NAME" "$GADGET_DIR/configs/c.1"

# ============================================================
# Step 5: Create FunctionFS instance (for HID only)
# ============================================================
echo "[setup] Creating FunctionFS HID function..."
mkdir -p "$GADGET_DIR/functions/$FFS_FUNC_NAME"

# Link FFS Function to Config
ln -s "$GADGET_DIR/functions/$FFS_FUNC_NAME" "$GADGET_DIR/configs/c.1"

# ============================================================
# Step 6: Mount FunctionFS
# ============================================================
mkdir -p "$MOUNT_DIR"
echo "[setup] Mounting FunctionFS at $MOUNT_DIR..."
mount -t functionfs ds "$MOUNT_DIR"

# Verify mount
if ! mountpoint -q "$MOUNT_DIR"; then
    echo "[setup] ERROR: FunctionFS mount failed!"
    exit 1
fi

# Verify ep0 exists
if [ ! -e "$MOUNT_DIR/ep0" ]; then
    echo "[setup] ERROR: ep0 not found after mount!"
    exit 1
fi

echo "[setup] Gadget setup complete. FFS mounted at $MOUNT_DIR."
echo "[setup] NOTE: UDC binding must be done by the daemon after writing descriptors."
echo "[setup] Available UDC: $(ls /sys/class/udc/)"
