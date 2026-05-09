#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RULE_FILE="99-hide-physical-dualsense.rules"

echo "[udev] Instalando a regra para esconder o DualSense Físico..."
sudo cp "$SCRIPT_DIR/$RULE_FILE" /etc/udev/rules.d/

echo "[udev] Recarregando as regras..."
sudo udevadm control --reload-rules
sudo udevadm trigger

echo "[udev] Feito! O DualSense físico (Bluetooth) agora será invisível para os jogos."
