#!/bin/bash
# setup_gadget.sh — Create USB Gadget (DualSense HID + optional UAC1 Audio)
#
# This script:
# 1. Runs teardown to clean any stale state
# 2. Loads required kernel modules
# 3. Creates the composite gadget via ConfigFS
# 4. Detects UDC capabilities and conditionally adds UAC1
# 5. Mounts FunctionFS for HID endpoint access
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
UAC1_FLAG="/tmp/ds5_uac1_enabled"

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
# Step 2: Detect UDC capabilities (isochronous support)
# ============================================================
# dummy_hcd does NOT support isochronous endpoints, which UAC1 requires.
# We detect the UDC type and only enable UAC1 on real hardware UDCs.
UDC_NAME="$(ls /sys/class/udc/ | head -1)"
UAC1_ENABLED=false

if [ -z "$UDC_NAME" ]; then
    echo "[setup] ERROR: No UDC found!"
    exit 1
fi

# Check if this is a dummy_hcd UDC (no isochronous support)
if echo "$UDC_NAME" | grep -q "dummy_udc"; then
    echo "[setup] Detected dummy_hcd UDC ($UDC_NAME) — no isochronous support."
    echo "[setup] UAC1 audio will be DISABLED (HID only)."
    UAC1_ENABLED=false
else
    echo "[setup] Detected real UDC ($UDC_NAME) — isochronous support available."
    echo "[setup] UAC1 audio will be ENABLED."
    UAC1_ENABLED=true
    modprobe usb_f_uac1 2>/dev/null || true
    sleep 0.2
fi

# ============================================================
# Step 3: Create gadget directory and set device identity
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
# Step 4: Create Config
# ============================================================
mkdir -p "$GADGET_DIR/configs/c.1"
echo 250 > "$GADGET_DIR/configs/c.1/MaxPower" # 500mA
mkdir -p "$GADGET_DIR/configs/c.1/strings/0x409"
echo "Config 1" > "$GADGET_DIR/configs/c.1/strings/0x409/configuration"

# ============================================================
# Step 5: Conditionally create UAC1 Audio Function
# DualSense audio: Speaker 4ch 48kHz 16-bit, Mic 2ch 48kHz 16-bit
# Only enabled when UDC supports isochronous endpoints.
# ============================================================
if [ "$UAC1_ENABLED" = true ]; then
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
else
    echo "[setup] Skipping UAC1 audio (UDC does not support isochronous endpoints)."
fi

# ============================================================
# Step 6: Create FunctionFS instance (for HID only)
# ============================================================
echo "[setup] Creating FunctionFS HID function..."
mkdir -p "$GADGET_DIR/functions/$FFS_FUNC_NAME"

# Link FFS Function to Config
ln -s "$GADGET_DIR/functions/$FFS_FUNC_NAME" "$GADGET_DIR/configs/c.1"

# ============================================================
# Step 7: Mount FunctionFS
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

# ============================================================
# Step 8: Write UAC1 flag for the daemon to read
# ============================================================
if [ "$UAC1_ENABLED" = true ]; then
    echo "1" > "$UAC1_FLAG"
else
    echo "0" > "$UAC1_FLAG"
fi

echo "[setup] Gadget setup complete. FFS mounted at $MOUNT_DIR."
echo "[setup] UAC1 audio: $([ "$UAC1_ENABLED" = true ] && echo ENABLED || echo DISABLED)"
echo "[setup] NOTE: UDC binding must be done by the daemon after writing descriptors."
echo "[setup] Available UDC: $(ls /sys/class/udc/)"
