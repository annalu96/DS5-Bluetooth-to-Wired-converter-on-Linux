echo "Procurando e bloqueando interfaces do DualSense..."

# 1. Bloquear interfaces no /dev/input (eventos, joysticks e touchpad)
for dev_path in /sys/class/input/event* /sys/class/input/js* /sys/class/input/mouse*; do
    if [ -e "$dev_path/device/name" ]; then
        if grep -qi "DualSense" "$dev_path/device/name"; then
            dev_node="/dev/input/$(basename "$dev_path")"
            echo "-> Removendo ACL e bloqueando leitura de: $dev_node"
            sudo setfacl -b "$dev_node" 2>/dev/null
            sudo chmod 0600 "$dev_node"
        fi
    fi
done

# 2. Bloquear interfaces brutas (hidraw)
for hid_path in /sys/class/hidraw/hidraw*; do
    if [ -e "$hid_path/device/uevent" ]; then
        # Busca pelos IDs da Sony (0CE6 para o Padrão, 0DF2 para o Edge)
        if grep -qi "054C:0CE6\|054C:0DF2" "$hid_path/device/uevent"; then
            dev_node="/dev/$(basename "$hid_path")"
            echo "-> Removendo ACL e bloqueando leitura de: $dev_node"
            sudo setfacl -b "$dev_node" 2>/dev/null
            sudo chmod 0600 "$dev_node"
        fi
    fi
done

echo "Concluído!"