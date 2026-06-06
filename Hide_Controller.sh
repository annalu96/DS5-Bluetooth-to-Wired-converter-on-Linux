echo "Procurando e bloqueando interfaces do DualSense Bluetooth..."
echo "(Dispositivos USB/gadget virtual serão preservados)"

# 1. Bloquear interfaces no /dev/input (eventos, joysticks e touchpad)
#    Somente dispositivos cujo caminho sysfs passa por um HID Bluetooth (0005:054C:)
for dev_path in /sys/class/input/event* /sys/class/input/js* /sys/class/input/mouse*; do
    if [ -e "$dev_path/device/name" ]; then
        if grep -qi "DualSense" "$dev_path/device/name"; then
            # Verificar se o caminho sysfs real contém "0005:054C:" (Bluetooth Sony)
            real_path=$(readlink -f "$dev_path" 2>/dev/null)
            if echo "$real_path" | grep -q "0005:054C:"; then
                dev_node="/dev/input/$(basename "$dev_path")"
                echo "-> Removendo ACL e bloqueando leitura de: $dev_node (BT)"
                sudo setfacl -b "$dev_node" 2>/dev/null
                sudo chmod 0600 "$dev_node"
            else
                echo "   Preservando: /dev/input/$(basename "$dev_path") (gadget USB virtual)"
            fi
        fi
    fi
done

# 2. Bloquear interfaces brutas (hidraw) — somente Bluetooth (bus 0005)
for hid_path in /sys/class/hidraw/hidraw*; do
    if [ -e "$hid_path/device/uevent" ]; then
        # Busca pelos IDs da Sony (0CE6 para o Padrão, 0DF2 para o Edge)
        # APENAS no bus Bluetooth (HID_ID começa com 0005:)
        if grep -q "^HID_ID=0005:" "$hid_path/device/uevent" && \
           grep -qi "054C:0CE6\|054C:0DF2" "$hid_path/device/uevent"; then
            dev_node="/dev/$(basename "$hid_path")"
            echo "-> Removendo ACL e bloqueando leitura de: $dev_node (BT)"
            sudo setfacl -b "$dev_node" 2>/dev/null
            sudo chmod 0600 "$dev_node"
        elif grep -qi "054C:0CE6\|054C:0DF2" "$hid_path/device/uevent"; then
            echo "   Preservando: /dev/$(basename "$hid_path") (gadget USB virtual)"
        fi
    fi
done

echo "Concluído!"