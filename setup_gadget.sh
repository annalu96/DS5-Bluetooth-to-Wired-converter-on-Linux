#!/bin/bash

# Configuration
GADGET_NAME="dualsense"
GADGET_DIR="/sys/kernel/config/usb_gadget/$GADGET_NAME"
MOUNT_DIR="/dev/ffs"
FUNC_NAME="ffs.ds"

# Load required modules
modprobe dummy_hcd 2>/dev/null || true
modprobe libcomposite 2>/dev/null || true
modprobe usb_f_fs 2>/dev/null || true

# Unbind if already exists
if [ -e "$GADGET_DIR/UDC" ] && [ -n "$(cat "$GADGET_DIR/UDC")" ]; then
    echo "" > "$GADGET_DIR/UDC"
fi

# Unmount if already mounted (MUST BE DONE BEFORE DELETING FUNCTION)
umount "$MOUNT_DIR" 2>/dev/null || true

# Clean up existing gadget if necessary
if [ -d "$GADGET_DIR" ]; then
    rm -f "$GADGET_DIR/configs/c.1/$FUNC_NAME"
    rmdir "$GADGET_DIR/configs/c.1/strings/0x409" 2>/dev/null
    rmdir "$GADGET_DIR/configs/c.1" 2>/dev/null
    rmdir "$GADGET_DIR/functions/$FUNC_NAME" 2>/dev/null
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

# Create FunctionFS instance
mkdir -p "$GADGET_DIR/functions/$FUNC_NAME"

# Link Function to Config
ln -s "$GADGET_DIR/functions/$FUNC_NAME" "$GADGET_DIR/configs/c.1"

# Prepare mount directory
mkdir -p "$MOUNT_DIR"

# Mount FunctionFS
mount -t functionfs ds "$MOUNT_DIR"

echo "Gadget setup complete. FFS mounted at $MOUNT_DIR. Ready to bind."
