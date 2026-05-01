# Relatório de Implementação: Migração do Pico2W DualSense Bridge para Linux (CachyOS)

Este relatório detalha as fases necessárias para converter o projeto de bridge Bluetooth-USB do Raspberry Pi Pico 2W para um ambiente Linux nativo (focado no CachyOS). O objetivo é criar um "Converter" em software que utilize as interfaces do kernel do Linux para simular a mesma conexão física, roteando os dados do DualSense via Bluetooth exclusivo para um dispositivo USB virtual interno.

---

## Arquitetura Geral

O sistema atuará como um intermediário (Daemon em espaço de usuário) entre o rádio Bluetooth e o subsistema USB do kernel Linux:

1.  **Entrada de Dados:** DualSense -> Bluetooth (Controle Exclusivo / Raw HCI) -> `Converter` -> USB Virtual (Gadget via `dummy_hcd`) -> Steam/Proton.
2.  **Saída de Dados (Rumble/Triggers):** Steam/Proton -> USB Virtual -> `Converter` -> Bluetooth (Raw HCI) -> DualSense.
3.  **Roteamento de Áudio:** Steam/Proton -> Placa de Som USB Virtual -> `Converter` (Opus Encode) -> Bluetooth -> DualSense (Headphone/Speaker).

---

## Fase 1: Preparação do Ambiente e Infraestrutura do Kernel

Para simular hardware USB via software no mesmo PC, precisaremos de módulos específicos do kernel do Linux.

*   **Módulo `dummy_hcd` (Dummy Host Controller):** Permite criar periféricos USB inteiramente por software que o kernel enxergará como dispositivos conectados a uma porta USB física.
*   **Módulo `libcomposite` / ConfigFS:** Usado para definir os descritores do nosso dispositivo USB (combinando HID para os controles e UAC para a placa de áudio).
*   **Módulo `usb_f_fs` (FunctionFS) ou Raw Gadget:** Permite que o nosso código em C++ (em espaço de usuário) injete e leia os dados dos endpoints (IN/OUT) do dispositivo USB criado.

**Ferramentas e Comandos Iniciais:**
```bash
# Carregar o módulo de controle de host virtual
sudo modprobe dummy_hcd

# Carregar módulos do ConfigFS para criação do Gadget
sudo modprobe libcomposite
```

---

## Fase 2: Análise e Reescrita do Código Atual

O código atual foi construído sobre o Pico SDK, BTstack e TinyUSB. Muitas bibliotecas e lógicas de baixo nível precisarão ser substituídas por APIs do Linux.

### O que precisa ser reescrito:

1.  **`src/main.cpp` e Loop de Eventos:**
    *   *Atual:* Usa `while(1)` com `tud_task()` e interrupções.
    *   *Novo:* Deve usar multiplexação de I/O do Linux, como `epoll` ou `poll`, aguardando eventos dos sockets Bluetooth e dos descritores de arquivo (FDs) do FunctionFS simultaneamente.
2.  **`src/usb.cpp`, `src/usb.h` e `src/usb_descriptors.c`:**
    *   *Atual:* Usa o TinyUSB (`tud_*`) para relatórios HID e leitura/configuração de Áudio (UAC1).
    *   *Novo:* O TinyUSB será **totalmente removido**. No Linux, a configuração dos descritores USB (Vendor ID, Product ID, HID report maps) é feita montando um sistema de arquivos (`/sys/kernel/config/usb_gadget/...`). A comunicação (envio de reports HID e recepção de Áudio) será feita lendo e escrevendo nos arquivos de endpoint `/dev/ffs/ep1`, `/dev/ffs/ep2`, etc. fornecidos pelo FunctionFS.
3.  **`src/bt.cpp` e `src/bt.h`:**
    *   *Atual:* Depende do BTstack (`pico_btstack_classic`) lidando com L2CAP.
    *   *Novo:* Substituído pela API do BlueZ utilizando sockets RAW HCI ou `HCI_CHANNEL_USER` (que permite assumir controle total do dongle Bluetooth). A biblioteca padrão do C (`<bluetooth/bluetooth.h>`, `<bluetooth/hci.h>`) será utilizada.
4.  **`src/audio.cpp`:**
    *   *Atual:* Lê do buffer USB usando `tud_audio_read()`, passa pelo resampler e Opus encoder.
    *   *Novo:* Lerá pacotes PCM brutos diretamente do endpoint OUT de áudio do FunctionFS (ex: `/dev/ffs/ep3_out`). O `WDL_Resampler` e o `OpusEncoder` do código original podem e devem ser mantidos sem alterações.
5.  **`src/utils.h`:**
    *   *Atual:* Estruturas de dados (ex: `USBGetStateData`, CRCs, CRC32).
    *   *Novo:* Podem ser reaproveitadas quase em sua totalidade (pois representam o protocolo do controle e do DualSense).

---

## Fase 3: Roteamento de Dados - Implementação do Bluetooth Bridge (Controle Exclusivo)

A exigência é tomar o controle do Bluetooth. Para fazer isso, o "Converter" deve "roubar" o adaptador do BlueZ padrão do sistema para enviar comandos crus (HCI).

**Passos de Implementação (C/C++):**
1. Derrubar a interface BlueZ para que o sistema não interfira:
   ```bash
   sudo hciconfig hci0 down
   ```
2. No C++, abrir um socket assumindo o canal do usuário:
   ```cpp
   #include <bluetooth/bluetooth.h>
   #include <bluetooth/hci.h>
   #include <bluetooth/hci_lib.h>

   int dev_id = hci_get_route(NULL);
   struct sockaddr_hci a = {0};
   a.hci_family = AF_BLUETOOTH;
   a.hci_dev = dev_id;
   a.hci_channel = HCI_CHANNEL_USER; // <-- Controle exclusivo do dispositivo

   int sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
   bind(sock, (struct sockaddr *) &a, sizeof(a));
   ```
3. A partir daqui, as requisições de pareamento, criação de conexão ACL, canais L2CAP para os PSMs `PSM_HID_CONTROL` (0x11) e `PSM_HID_INTERRUPT` (0x13) do DualSense devem ser geridas no C++, adaptando o código de `bt.cpp`.

---

## Fase 4: Implementação do USB Gadget (Dispositivo Virtual e Roteamento)

O "Converter" vai instanciar um dispositivo USB com interface composta (HID + UAC) no ConfigFS e usar o FunctionFS para rotear os dados.

**Estruturação via Script/C++ (ConfigFS):**
1. O programa cria as pastas em `/sys/kernel/config/usb_gadget/dualsense/`.
2. Insere os descritores HID extraídos de `usb_descriptors.c` do código antigo.
3. Configura UAC (USB Audio Class) para receber os canais de áudio.
4. Liga (bind) o Gadget ao `dummy_udc.0` (criado pelo `dummy_hcd`).

**Roteamento (O Loop Principal):**
*   **Leitura BT -> Escrita USB (Inputs e Sensores):**
    Quando um pacote L2CAP do PSM `0x13` (Interrupt) chega no socket BT (`read(bt_sock, ...)`), o "Converter" processa e injeta no endpoint USB: `write(ep_hid_in, relatorio_dual_sense, 63);`. Para a Steam e Proton, isso parece uma mensagem IN de um cabo físico.
*   **Leitura USB -> Escrita BT (Rumble/Adapt. Triggers):**
    O Converter monitora o `ep_hid_out`. Ao receber pacotes USB da Steam (ex: Report ID `0x02` para triggers e leds), ele reempacota para envio no socket L2CAP do BT: `send(bt_sock, bt_packet, size, 0);`.

---

## Fase 5: Integração da Placa de Áudio Virtual

O áudio não precisa interagir diretamente com o PipeWire ou PulseAudio do CachyOS através de suas APIs específicas (como `libpipewire`).

Graças à arquitetura do USB Gadget (Fase 4):
1. O Gadget exporá uma "Function Audio" UAC1/UAC2 ao kernel.
2. O sistema CachyOS conectará virtualmente essa placa de som através do `dummy_hcd`.
3. O **PipeWire** (gerenciador de áudio do CachyOS) detectará automaticamente a nova placa de som chamada "DualSense Audio Virtual" e permitirá que os jogos / Steam direcionem áudio para ela.

**O Fluxo de Processamento (no C++):**
*   O programa monitorará o Endpoint de Áudio OUT (`/dev/ffs/ep_audio_out`).
*   Lerá o stream contínuo de áudio LPCM gerado pela Steam/Proton.
*   Repassará os frames pelo `WDL_Resampler` (48KHz -> 32KHz/48KHz).
*   Codificará usando a lib `opus_encode` (já presente no código).
*   Empacotará (juntando possivelmente com o haptics como em `src/audio.cpp`) e despachará para o controle via L2CAP Bluetooth.

---

## Conclusão e Resumo de Bibliotecas Necessárias (CachyOS)
Para compilar e gerenciar a nova arquitetura, o ambiente CachyOS necessitará de:
*   `linux-headers` (Para módulos dummy_hcd e definições do kernel).
*   `bluez` e `bluez-libs` (Para `<bluetooth/hci.h>`).
*   Opcional: `libusbgx` (Biblioteca em C para facilitar a manipulação do ConfigFS/FunctionFS, abstraindo os comandos do terminal).
*   `libopus-dev` (Para a compressão de áudio).

---

## Fase 5 - Status Atual (Code Review)

A implementação da **Fase 5** migrou com sucesso o tratamento de áudio que outrora era gerenciado pelo Pico SDK (TinyUSB) para o ambiente Linux utilizando FunctionFS. Abaixo, pontuamos em detalhes o que foi implementado, os problemas identificados e os próximos passos.

### Features Implementadas

1. **Multiplexação no Linux (`epoll`):**
   O daemon agora utiliza `epoll` dentro do arquivo `src/main.cpp` para monitorar eventos. A antiga task do TinyUSB (`tud_task()`) foi substituída por um gerenciamento assíncrono que aguarda os sockets Bluetooth (`L2CAP`) e descritores USB do FunctionFS simultaneamente.

2. **Endpoints de Áudio UAC (FunctionFS):**
   O arquivo `src/usb.cpp` foi alterado para abrir e montar os descritores do Gadget virtual.
   - `ep_audio_out_fd` foi mapeado para `/dev/ffs/ep1` (Para receber o fluxo de Áudio do Host (Steam/Proton)).
   - `ep_audio_in_fd` foi mapeado para `/dev/ffs/ep2` (Para enviar o fluxo de Microfone ao Host).

3. **Injeção de Descritores do Gadget USB:**
   A configuração de dispositivo Composto (HID + UAC) agora é feita pelo preenchimento das estruturas `descriptors` do FunctionFS que são escritas em `ep0_fd` (`/dev/ffs/ep0`). Os tamanhos variáveis como os relatórios HID foram adaptados e os scripts `.py` auxiliaram no dump do `old_usb_descriptors.c`.

4. **Recebimento de Áudio LPCM (`audio_receive_pcm`):**
   O `epoll` monitora `ep_audio_out_fd` (`EPOLLIN`). Quando recebe dados, lê blocos de pacotes com tamanho máximo definido e passa a função `audio_receive_pcm()` do `src/audio.cpp`.

5. **Tratamento e Compressão do Áudio:**
   Foi mantido o funcionamento do `WDL_Resampler` (`lib/WDL`) e o encoder do `Opus` (`lib/opus`). O som LPCM resampleado passa pela thread de áudio e é comprimido por OPUS antes de ser empacotado para o DualSense em `src/audio.cpp`.

6. **Script `setup_gadget.sh`:**
   Desenvolvido para criar o Virtual UDC. Agora é evocado pelo `system()` durante o `usb_init()`.

### Features Faltantes e Bugs Identificados (Ações Necessárias)

1. **Problemas com as permissões de acesso ao Script `setup_gadget.sh`:**
   Atualmente a função `usb_init()` chama `system("./setup_gadget.sh")`. Este design obriga que o daemon em C++ seja executado como `root` por causa dos comandos do script (`modprobe dummy_hcd` e criação de pastas em `/sys/kernel/config/usb_gadget`), além do script poder quebrar caso os caminhos (Current Working Directory) não apontem precisamente para `./setup_gadget.sh`. Pode ser recomendável criar o gadget USB fora da execução em C++.

2. **Configuração de Build Original:**
   Existem arquivos referentes ao sistema de build antigo para compilar `.uf2` para o microcontrolador do Pico, tanto no Github Actions quanto no ambiente local, que são incompatíveis com o Linux.

3. **Problema no loop do `epoll` em `main.cpp` (Atenção no Polling e Buffer):**
   Atualmente no `src/main.cpp`, quando ocorre uma notificação `EPOLLIN` em `ep_audio_out_fd`, ele consome 392 bytes de áudio de uma só vez (`int16_t buf[196]`). Mas, o tamanho real da leitura (`ret = read(...)`) e o limite estipulado nas variáveis não tem verificações pesadas para perdas de desync / frames parciais (Packet Drops).

---

## Arquivos que Podem Ser Removidos

Alguns artefatos e scripts auxiliares desenvolvidos especificamente para as transições anteriores ou que são exclusivos do Raspberry Pi Pico 2W, e não serão utilizados na stack do daemon (CachyOS), podem ser removidos do repositório:

- `.github/workflows/build.yml` (e possivelmente `release.yml`, pois compila para `arm-none-eabi` gerando um `.uf2` do Raspberry Pi Pico, incompatível com a versão Linux daemon).
- `bt_a3_verify.py`
- `count_descriptors.py`
- `descriptors_struct_dump.txt`
- `generate_descriptors.py`
- `main_interrupt_in.py`
- `main_interrupt_out.py`
- `offset_finder.py`, `offset_finder2.py`, `offset_finder3.py`
- `patch_main.py`
- `patch_main_1.diff`
- `patch_script.sh`
- `patch_usb.diff`, `patch_usb_2.diff`, `patch_usb_bt.diff`
- `patch_usb.py`, `patch_usb_h.py`
- `plan.txt`
- `setup_env.sh` (Pode ser mantido se quiser automatizar os módulos, porém muitas vezes não faz parte da build `cmake` em si).
- `test_plan.sh`
- `test_plan2.py`
- `usb_handle_ep0_replace.py`
- `usb_handle_ep0_replace_2.py`

*(Nota: Todos os arquivos que iniciam com `old_` estão preservados no repositório).*
