# Instruções de Execução (CachyOS)

Este guia descreve os passos necessários para compilar e executar o daemon `dualsense_bridge` em um ambiente Linux (focado no CachyOS / Arch Linux).

## Dependências Necessárias

Para compilar e executar o projeto, você precisará das seguintes ferramentas e bibliotecas instaladas no sistema:

```bash
sudo pacman -S base-devel cmake pkgconf linux-headers bluez bluez-libs opus
```
*(Nota: dependendo da sua versão de kernel no CachyOS, o pacote `linux-headers` pode ter outro nome como `linux-cachyos-headers`)*

## 1. Compilando o Projeto

1. Clone o repositório e crie o diretório de build:
   ```bash
   mkdir build
   cd build
   cmake ..
   make
   ```
2. Após compilar, você terá o binário `dualsense_bridge` dentro da pasta `build/`.

## 2. Preparando os Módulos do Kernel

O daemon utiliza o subsistema USB Gadget do Linux para emular o controle e a placa de som.
Os seguintes módulos precisam estar carregados. O script `setup_gadget.sh` tentará carregá-los, mas caso precise carregar manualmente:

```bash
sudo modprobe dummy_hcd
sudo modprobe libcomposite
sudo modprobe usb_f_fs
```

*Nota: O `dummy_hcd` é o módulo responsável por criar uma porta USB virtual no Linux onde nosso Gadget (Controle + Áudio) será conectado.*

## 3. Preparando os Sockets Bluetooth (HCI)

Se o daemon apresentar erros na hora de criar os sockets Bluetooth ou conectar:
Certifique-se de que o serviço de bluetooth e dbus estão rodando no sistema:

```bash
sudo systemctl enable --now bluetooth.service
```

## 4. Executando o Daemon

Atualmente, o script de preparação do Gadget (`setup_gadget.sh`) é invocado dentro do próprio C++. Devido à manipulação do `ConfigFS` (`/sys/kernel/config/usb_gadget`), o daemon **precisa** ser executado como superusuário (root).

Certifique-se de executar o daemon do diretório onde ele consiga localizar o `setup_gadget.sh` (normalmente de dentro da pasta raiz ou dentro da pasta `build/` caso o código faça fallback para `../setup_gadget.sh`).

```bash
sudo ./dualsense_bridge
```

## 5. Como o Sistema (Steam/Proton) irá Reconhecer

- **Controle:** Aparecerá para os jogos um controle Sony DualSense como se estivesse fisicamente conectado por um cabo USB. O Linux o mapeará em `/dev/input/`.
- **Áudio:** O *PipeWire* ou *PulseAudio* irá detectar uma nova placa de som chamada "DualSense Audio Virtual" conectada via USB virtual.
- O sistema da Steam ou do próprio jogo irá disparar o áudio para o Endpoint virtual e o `dualsense_bridge` irá comprimir em *OPUS* e enviar via Bluetooth ao DualSense real.
