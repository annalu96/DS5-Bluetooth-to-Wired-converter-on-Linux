#!/bin/bash
set -e

RULE_FILE="99-hide-physical-dualsense.rules"
DEST_PATH="/etc/udev/rules.d/$RULE_FILE"

if [ -f "$DEST_PATH" ]; then
    echo "[udev] Removendo a regra $DEST_PATH..."
    sudo rm "$DEST_PATH"
    
    echo "[udev] Recarregando as regras do sistema..."
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    
    echo "[udev] Desfeito com sucesso! O DualSense físico (Bluetooth) voltará a ser detectado pelos jogos normalmente."
else
    echo "[udev] A regra não foi encontrada em $DEST_PATH. Nenhuma ação necessária."
fi
