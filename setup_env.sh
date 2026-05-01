#!/bin/bash

# Script de preparação do ambiente para o "Converter" do DualSense (CachyOS / Arch Linux)

# Habilita parada imediata caso algum comando falhe
set -e
set -u
set -o pipefail

# Verifica se o script está sendo executado como root
if [ "$EUID" -ne 0 ]; then
  echo "ERRO: Por favor, execute como root (ex: sudo ./setup_env.sh)"
  exit 1
fi

echo "[1/3] Atualizando repositórios e instalando dependências (focado em CachyOS/Arch Linux)..."
# Nota: libopus-dev no Debian é o pacote opus no Arch
if ! pacman -Syu --noconfirm --needed linux-headers bluez bluez-libs opus cmake gcc pkgconf; then
  echo "ERRO: Falha ao instalar as dependências com o pacman."
  exit 1
fi

echo "[2/3] Configurando o carregamento automático dos módulos no boot..."
MODULES_CONF="/etc/modules-load.d/dualsense_bridge.conf"
mkdir -p /etc/modules-load.d
echo "dummy_hcd" > "$MODULES_CONF"
echo "libcomposite" >> "$MODULES_CONF"
echo "usb_f_fs" >> "$MODULES_CONF"
echo "Arquivo $MODULES_CONF criado/atualizado com sucesso."

echo "[3/3] Carregando módulos do kernel imediatamente..."
# Função para tentar carregar o módulo e verificar sucesso
load_module() {
    local module=$1
    if ! modprobe "$module"; then
        echo "ERRO: Falha ao carregar o módulo $module."
        exit 1
    fi
}

# dummy_hcd: Cria o controlador de host virtual
load_module "dummy_hcd"
# libcomposite: Permite criar dispositivos USB compostos via ConfigFS
load_module "libcomposite"
# usb_f_fs: FunctionFS, permite criar funções USB em espaço de usuário
load_module "usb_f_fs"

echo "Verificação dos módulos carregados:"
if ! lsmod | grep -E "dummy_hcd|libcomposite|usb_f_fs"; then
   echo "AVISO: modprobe funcionou, mas lsmod não encontrou os módulos. (Ignorar se rodando em container)."
fi

echo "Ambiente preparado com sucesso para as Fases 2, 3 e 4!"
