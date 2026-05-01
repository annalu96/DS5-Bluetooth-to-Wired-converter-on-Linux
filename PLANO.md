# Plano de Migração: DualSense 5 Bridge (Pico 2W para Linux/CachyOS)

## Visão Geral
O objetivo deste documento é detalhar as fases de engenharia para portar o projeto `ds5-bridge` — originalmente escrito em C/C++ usando o Pico SDK para rodar em um Raspberry Pi Pico 2W — para um software nativo de PC rodando em Linux (CachyOS).

Nessa nova arquitetura, o programa em Rust rodará no PC, conectando-se diretamente ao controle DualSense pelo adaptador de Bluetooth do computador e simulando um DualSense cabeado virtualmente por meio da interface VHCI (USB/IP) do kernel Linux. Isso permite que jogos na Steam/Proton detectem um controle de fio tradicional e repassem o "HD Rumble" (áudio haptic), os Adaptive Triggers e outras funcionalidades, que o projeto retransmitirá ao controle via Bluetooth.

## Decisões Tecnológicas Base
* **Linguagem de Programação:** Rust (segurança de memória, paralelismo assíncrono avançado e excelente integração de baixo nível no Linux).
* **Bluetooth (BlueZ):** Conexão direta usando L2CAP Sockets do sistema, acessados através de abstrações como o crate `bluer` ou acesso direto via libc/sys `AF_BLUETOOTH`.
* **USB Virtual (Entrada e Áudio):** Módulo de kernel **VHCI (Virtual Host Controller Interface)** via protocolo USB/IP em user-space. Isso permite simular todo o hardware USB — tanto entradas HID quanto as interfaces de Áudio Class (Placa de Som).
* **Subsistema Assíncrono:** `tokio` para manipulação de eventos simultâneos (I/O USB/IP, I/O Bluetooth, tarefas pesadas de codificação de áudio).

---

## Fases de Desenvolvimento

### Fase 1: Fundação do Projeto Rust e Conectividade Bluetooth (BlueZ)
**Objetivo:** Estabelecer a base do projeto e garantir comunicação bidirecional de baixo nível com o controle DualSense via adaptador Bluetooth do PC.

1. **Estrutura Básica:** Inicializar o projeto usando `cargo` e configurar o workspace.
2. **Pilha Bluetooth:** Implementar a descoberta e emparelhamento.
3. **Canais L2CAP:** Assim como em `bt.cpp`, estabelecer os sockets nos canais essenciais do DualSense:
   * **PSM_HID_CONTROL (0x11):** Usado para ler e setar configurações via Feature Reports (ex: cores do LED RGB, MAC address, relatórios GET/SET).
   * **PSM_HID_INTERRUPT (0x13):** Tráfego crítico em tempo real para os comandos de entrada (Input Reports) dos botões, analógicos e para enviar a telemetria, Rumble e Áudio.
4. **Gerenciamento de Estado:** Tratar conexões, desconexões e o contador inativo (sleep timeout de desconexão, silenciando mic, etc.).

### Fase 2: Implementação da Camada USB Virtual (VHCI)
**Objetivo:** Simular fisicamente um dispositivo USB conectado no host, capaz de enganar ferramentas e motores de jogos para atuar como o dispositivo original.

1. **Servidor USB/IP:** Criar o protocolo de handshake e as rotinas para que o driver nativo de kernel do Linux `vhci-hcd` acesse nosso programa de espaço do usuário.
2. **Tradução dos Descritores USB (`usb_descriptors.c`):**
   * Migrar os descritores C originais para representações em Rust.
   * **Device Descriptor:** Definir o `idVendor` (Sony) e `idProduct` para mimetizar o DualSense.
   * **Configuration/Interface Descriptors:** Informar ao Kernel sobre as interfaces emuladas (Audio Control, Audio Streaming para Mic e Speaker, e a interface HID principal).
   * **HID Report Descriptors:** Replicar as exatas 405 bytes ou estruturas mapeadas em C para os botões.
3. **Validação Preliminar:** Ao rodar a aplicação, um novo dispositivo de áudio (Placa de som DualSense virtual) e um novo controle (HID) deverão aparecer nas listagens do sistema (`lsusb` e no subsistema `ALSA/PulseAudio/PipeWire`).

### Fase 3: Roteamento de Dados Bidirecional (O 'Converter')
**Objetivo:** Interligar as duas pontas. Transformar "Bluetooth In" em "USB In" e "USB Out" em "Bluetooth Out".

1. **Input Pipeline (Entrada de Dados - Controle -> PC):**
   * Escutar dados do canal L2CAP Interrupt.
   * Parsear relatórios Bluetooth de Input e extrair os blocos de dados.
   * Encapsular como "Interrupt IN Endpoint Data" sob o formato cabeado esperado e enviar para a stack VHCI/USB.
2. **Output Pipeline (Saída de Dados - Jogo/Proton -> Controle):**
   * Receber pelo servidor USB/IP as escritas no "Interrupt OUT Endpoint" provenientes do jogo. (ex: Comandos de LED e vibração tradicional/Adaptive Triggers).
   * Aplicar as transformações necessárias para a versão Bluetooth: adicionar o Report ID (`0x31`), Byte de Sequenciamento e efetuar o cálculo de Checksum CRC32 da Sony no final do Report.
   * Transmitir no canal L2CAP do Bluetooth de volta ao controle.

### Fase 4: Subsistema de Áudio Haptic e Placa de Som Virtual
**Objetivo:** Roteamento do som gerado pela Steam/Jogos para acionar motores Haptic e o falante do controle. No Linux, a Placa de Som Virtual embutida no VHCI atuará como "sink" de áudio.

1. **Captura de Áudio (Isócrono USB):**
   * O sistema operacional repassará o áudio (4 Canais a 48kHz via Endpoints isócronos da Placa de Som Virtual) para o nosso projeto Rust.
2. **Processamento e Resampling (Baseado em `audio.cpp`):**
   * Extrair os canais corretos (Speaker vs Haptics).
   * Substituir o C++ `WDL_Resampler` por bibliotecas de reamostragem em Rust (ex: `rubato`), convertendo de 48kHz para os 3kHz ou formatos necessários pelo controle para Haptics de baixa frequência.
3. **Encoding:**
   * Utilizar bindings da biblioteca nativa `opus` (`opus-rs`) para comprimir o aúdio extraído (48000Hz, estéreo, bitrates/framesize equivalentes a `OPUS_FRAMESIZE_10_MS`).
4. **Multiplexação:**
   * Fundir o sinal de Rumble com o frame Opus comprimido em um pacote longo com o `Report ID 0x36`.
   * Agendar o envio via Bluetooth respeitando as exigências estritas de temporização do DualSense.

### Fase 5: Integração, Paralelismo e Estabilização Final
**Objetivo:** Unificar os módulos em uma aplicação coesa, performática e livre de _stutters_ de som ou comandos atrasados.

1. **Loops Assíncronos:** O `tokio` (ou similar) gerenciará _tasks_ separadas para: Listener do USB/IP, Worker L2CAP In/Out, e a Pipeline de Audio+Encoder.
2. **Tratamento de Permissões:** Configurar as _udev rules_ necessárias e garantir que o programa tenha permissão para acesso ao BlueZ e carga/anexação pelo `vhci-hcd`.
3. **Testes do Proton/Steam:** Testar o DualSense em jogos chave com o pacote Steam Input ou em jogos nativos do PlayStation (como Ghost of Tsushima, Returnal ou Spider-Man) que utilizam intensamente Rumble de HD e os gatilhos via USB.
