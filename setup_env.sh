#!/bin/bash

# Script de preparação do ambiente para o "Converter" do DualSense (CachyOS / Arch Linux)

# Verifica se o script está sendo executado como root
if [ "$EUID" -ne 0 ]; then
  echo "Por favor, execute como root (ex: sudo ./setup_env.sh)"
  exit 1
fi

echo "Atualizando repositórios e instalando dependências (focado em CachyOS/Arch Linux)..."
# Nota: libopus-dev no Debian é o pacote opus no Arch
pacman -Syu --noconfirm --needed linux-headers bluez bluez-libs opus cmake gcc pkgconf

echo "Carregando módulos do kernel..."
# dummy_hcd: Cria o controlador de host virtual
modprobe dummy_hcd
# libcomposite: Permite criar dispositivos USB compostos via ConfigFS
modprobe libcomposite
# usb_f_fs: FunctionFS, permite criar funções USB em espaço de usuário
modprobe usb_f_fs

echo "Módulos carregados:"
lsmod | grep -E "dummy_hcd|libcomposite|usb_f_fs"

echo "Ambiente preparado com sucesso para a Fase 2!"
