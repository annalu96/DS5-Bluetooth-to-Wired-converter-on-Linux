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

---

# Resolução de Problemas com Módulos do Kernel (CachyOS / Arch Linux)

Ao tentar iniciar a emulação USB para o controle DualSense, você pode se deparar com erros informando que módulos essenciais do kernel não foram encontrados, como por exemplo:
```
modprobe: FATAL: Module dummy_hcd not found in directory /lib/modules/...
modprobe: FATAL: Module libcomposite not found in directory /lib/modules/...
modprobe: FATAL: Module usb_f_fs not found in directory /lib/modules/...
```

Isso significa que os módulos responsáveis pela simulação da porta USB e dos "gadgets" USB não estão disponíveis no seu kernel atual. Abaixo estão os passos para diagnosticar e corrigir o problema.

## 1. Verifique se o sistema precisa ser reiniciado
Se você acabou de atualizar o sistema ou o kernel, os arquivos do módulo da nova versão foram instalados, mas você ainda está rodando o kernel antigo.

Execute o comando abaixo para ver qual kernel está rodando:
```bash
uname -r
```

E verifique a lista de módulos instalados na pasta:
```bash
ls /lib/modules/
```
**Solução:** Se as versões forem diferentes (por exemplo, `uname -r` retorna 7.0.2-2-cachyos, mas a pasta contém 7.0.3-cachyos), **reinicie o computador** para carregar o novo kernel.

## 2. Instale o pacote de Headers do Kernel
Algumas vezes, especialmente ao usar módulos que precisam ser compilados ou configurados (como DKMS, caso seja aplicável), os *headers* do kernel correspondente devem estar instalados.

Verifique qual kernel você usa:
```bash
uname -r
```

Se o seu kernel for, por exemplo, o `linux-cachyos`, você precisará do pacote `linux-cachyos-headers`. Instale com:
```bash
sudo pacman -S linux-cachyos-headers
```
Ou, se estiver usando um kernel genérico do Arch:
```bash
sudo pacman -S linux-headers
```

## 3. O Kernel CachyOS suporta "USB Gadget"?
O CachyOS utiliza kernels compilados com foco extremo em performance. Para otimizar o desempenho, às vezes os mantenedores do CachyOS desativam módulos específicos do kernel que são usados raramente por usuários comuns (como o `dummy_hcd` e o suporte a USB Gadget).

Para verificar se o seu kernel foi compilado com esse suporte, execute:
```bash
zgrep -i dummy_hcd /proc/config.gz
zgrep -i usb_gadget /proc/config.gz
```
- Se não retornar nada ou se aparecer `# CONFIG_USB_DUMMY_HCD is not set`, significa que o seu kernel atual simplesmente não tem esse recurso embutido nem como módulo.

**Solução (Troca de Kernel):**
Nesse caso, a solução mais garantida é instalar um kernel que suporte USB Gadgets (geralmente os kernels oficiais do Arch Linux ou versões como o `linux-zen`).

Para instalar o kernel padrão do Arch Linux, que possui o `dummy_hcd` habilitado:
```bash
sudo pacman -S linux linux-headers
```

Depois de instalar, atualize o seu gerenciador de boot (como o GRUB ou systemd-boot) para apontar para o novo kernel e **reinicie o computador**, escolhendo o kernel `linux` (padrão) na inicialização.

Após iniciar com o kernel compatível, execute novamente:
```bash
sudo modprobe dummy_hcd
sudo modprobe libcomposite
sudo modprobe usb_f_fs
```
E os comandos devem funcionar sem erros!
