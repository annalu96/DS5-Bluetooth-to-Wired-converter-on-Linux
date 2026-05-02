#!/bin/bash

# Configuration
GADGET_NAME="dualsense"
GADGET_DIR="/sys/kernel/config/usb_gadget/$GADGET_NAME"
MOUNT_DIR="/dev/ffs"
FFS_FUNC_NAME="ffs.ds"
UAC1_FUNC_NAME="uac1.usb0"

# Load required modules
modprobe dummy_hcd 2>/dev/null || true
modprobe libcomposite 2>/dev/null || true
modprobe usb_f_fs 2>/dev/null || true
modprobe usb_f_uac1 2>/dev/null || true

# Unbind if already exists
if [ -e "$GADGET_DIR/UDC" ] && [ -n "$(cat "$GADGET_DIR/UDC")" ]; then
    echo "" > "$GADGET_DIR/UDC"
fi

# Unmount if already mounted (MUST BE DONE BEFORE DELETING FUNCTION)
umount "$MOUNT_DIR" 2>/dev/null || true

# Clean up existing gadget if necessary
if [ -d "$GADGET_DIR" ]; then
    rm -f "$GADGET_DIR/configs/c.1/$FFS_FUNC_NAME"
    rm -f "$GADGET_DIR/configs/c.1/$UAC1_FUNC_NAME"
    rmdir "$GADGET_DIR/configs/c.1/strings/0x409" 2>/dev/null
    rmdir "$GADGET_DIR/configs/c.1" 2>/dev/null
    rmdir "$GADGET_DIR/functions/$FFS_FUNC_NAME" 2>/dev/null
    rmdir "$GADGET_DIR/functions/$UAC1_FUNC_NAME" 2>/dev/null
    rmdir "$GADGET_DIR/strings/0x409" 2>/dev/null
    rmdir "$GADGET_DIR" 2>/dev/null
fi

# Create gadget directory
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

# Create Config
mkdir -p "$GADGET_DIR/configs/c.1"
echo 250 > "$GADGET_DIR/configs/c.1/MaxPower" # 500mA
mkdir -p "$GADGET_DIR/configs/c.1/strings/0x409"
echo "Config 1" > "$GADGET_DIR/configs/c.1/strings/0x409/configuration"

# ============================================================
# Create UAC1 Audio Function (kernel-managed descriptors)
# DualSense audio: Speaker 4ch 48kHz 16-bit, Mic 2ch 48kHz 16-bit
# ============================================================
mkdir -p "$GADGET_DIR/functions/$UAC1_FUNC_NAME"

# Playback (host -> gadget -> speaker): 4 channels, 48kHz, 16-bit
# Channel mask 0x33 = FL+FR+SL+SR (Front Left, Front Right, Side Left, Side Right)
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
# Create FunctionFS instance (for HID only)
# ============================================================
mkdir -p "$GADGET_DIR/functions/$FFS_FUNC_NAME"

# Link FFS Function to Config
ln -s "$GADGET_DIR/functions/$FFS_FUNC_NAME" "$GADGET_DIR/configs/c.1"

# Prepare mount directory
mkdir -p "$MOUNT_DIR"

# Mount FunctionFS
mount -t functionfs ds "$MOUNT_DIR"

echo "Gadget setup complete. FFS mounted at $MOUNT_DIR. Ready to bind."
