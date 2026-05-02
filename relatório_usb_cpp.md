# Relatório de Descritores USB (usb.cpp)

Este documento apresenta a análise detalhada dos descritores USB fornecidos no arquivo `usb.cpp`. A estrutura define o comportamento de um dispositivo composto (Áudio + HID), configurado para emular as interfaces de um controle Sony DualSense (PS5).

> **Nota:** Os arrays `fs_desc` (Full-Speed) e `hs_desc` (High-Speed) possuem exatamente a mesma estrutura de bytes. Portanto, a análise a seguir aplica-se a ambos os modos de velocidade.


### Interface 0, Alternate 0 - Áudio Control/Streaming (0x01)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 0 | `0x09` | `bLength` | Tamanho do descritor (9 bytes) | Correto |
| 1 | `0x04` | `bDescriptorType` | Tipo Interface (0x04) | Correto |
| 2 | `0x00` | `bInterfaceNumber` | Número da Interface (0) | Correto (Interface 0) |
| 3 | `0x00` | `bAlternateSetting` | Alternate Setting (0) | Correto |
| 4 | `0x00` | `bNumEndpoints` | Quantidade de Endpoints (0) | Correto |
| 5 | `0x01` | `bInterfaceClass` | Classe (1) | Correto (Áudio Control/Streaming (0x01)) |
| 6 | `0x01` | `bInterfaceSubClass` | Subclasse (1) | Correto |
| 7 | `0x00` | `bInterfaceProtocol` | Protocolo (0) | Correto |
| 8 | `0x00` | `iInterface` | Índice de String (0) | Correto |

### Áudio Class-Specific Interface (CS_INTERFACE) - HEADER / AS_GENERAL (0x01)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 9 | `0x0A` | `bLength` | Tamanho do descritor (10 bytes) | Correto |
| 10 | `0x24` | `bDescriptorType` | CS_INTERFACE (0x24) | Correto |
| 11 | `0x01` | `bDescriptorSubtype` | Subtipo (0x01) | Correto |
| 12 | `0x00 0x01` | `bcdADC` | Versão UAC (1.00) | Correto (1.00 é padrão DualSense) |
| 14 | `0x49 0x00` | `wTotalLength` | Comprimento Total (73 bytes) | Correto |
| 16 | `0x02` | `bInCollection` | Total de Interfaces de Áudio (2) | Correto |
| 17 | `0x01` | `baInterfaceNr(1)` | Interface de Áudio ID (1) | Correto |
| 18 | `0x02` | `baInterfaceNr(2)` | Interface de Áudio ID (2) | Correto |

### Áudio Class-Specific Interface (CS_INTERFACE) - INPUT_TERMINAL / FORMAT_TYPE (0x02)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 19 | `0x0C` | `bLength` | Tamanho do descritor (12 bytes) | Correto |
| 20 | `0x24` | `bDescriptorType` | CS_INTERFACE (0x24) | Correto |
| 21 | `0x02` | `bDescriptorSubtype` | Subtipo (0x02) | Correto |
| 22 | `0x01` | `bTerminalID` | ID do Terminal (1) | Correto |
| 23 | `0x01 0x01` | `wTerminalType` | Tipo de Terminal (0x0101) | Correto (0x0101 = USB Streaming, 0x0201 = Microfone) |
| 25 | `0x06` | `bAssocTerminal` | Terminal Associado (6) | Correto |
| 26 | `0x04` | `bNrChannels` | Número de Canais (4) | Correto |
| 27 | `0x33 0x00` | `wChannelConfig` | Configuração de Canais | Correto |
| 29 | `0x00` | `iChannelNames` | String Index (0) | Correto |
| 30 | `0x00` | `iTerminal` | String Index (0) | Correto |

### Áudio Class-Specific Interface (CS_INTERFACE) - FEATURE_UNIT (0x06)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 31 | `0x0C` | `bLength` | Tamanho do descritor (12 bytes) | Correto |
| 32 | `0x24` | `bDescriptorType` | CS_INTERFACE (0x24) | Correto |
| 33 | `0x06` | `bDescriptorSubtype` | Subtipo (0x06) | Correto |
| 34 | `0x02` `0x01` `0x01` `0x03` `0x00` `0x00` `0x00` `0x00` `0x00` | `Dados Restantes` | - | Correto |

### Áudio Class-Specific Interface (CS_INTERFACE) - OUTPUT_TERMINAL (0x03)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 43 | `0x09` | `bLength` | Tamanho do descritor (9 bytes) | Correto |
| 44 | `0x24` | `bDescriptorType` | CS_INTERFACE (0x24) | Correto |
| 45 | `0x03` | `bDescriptorSubtype` | Subtipo (0x03) | Correto |
| 46 | `0x03` | `bTerminalID` | ID do Terminal (3) | Correto |
| 47 | `0x01 0x03` | `wTerminalType` | Tipo de Terminal (0x0301) | Correto (0x0301 = Speaker, 0x0101 = USB Streaming) |
| 49 | `0x04` | `bAssocTerminal` | Terminal Associado (4) | Correto |
| 50 | `0x02` | `bSourceID` | ID Origem (2) | Correto |
| 51 | `0x00` | `iTerminal` | String Index (0) | Correto |

### Áudio Class-Specific Interface (CS_INTERFACE) - INPUT_TERMINAL / FORMAT_TYPE (0x02)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 52 | `0x0C` | `bLength` | Tamanho do descritor (12 bytes) | Correto |
| 53 | `0x24` | `bDescriptorType` | CS_INTERFACE (0x24) | Correto |
| 54 | `0x02` | `bDescriptorSubtype` | Subtipo (0x02) | Correto |
| 55 | `0x04` | `bTerminalID` | ID do Terminal (4) | Correto |
| 56 | `0x02 0x04` | `wTerminalType` | Tipo de Terminal (0x0402) | Correto (0x0101 = USB Streaming, 0x0201 = Microfone) |
| 58 | `0x03` | `bAssocTerminal` | Terminal Associado (3) | Correto |
| 59 | `0x02` | `bNrChannels` | Número de Canais (2) | Correto |
| 60 | `0x03 0x00` | `wChannelConfig` | Configuração de Canais | Correto |
| 62 | `0x00` | `iChannelNames` | String Index (0) | Correto |
| 63 | `0x00` | `iTerminal` | String Index (0) | Correto |

### Áudio Class-Specific Interface (CS_INTERFACE) - FEATURE_UNIT (0x06)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 64 | `0x09` | `bLength` | Tamanho do descritor (9 bytes) | Correto |
| 65 | `0x24` | `bDescriptorType` | CS_INTERFACE (0x24) | Correto |
| 66 | `0x06` | `bDescriptorSubtype` | Subtipo (0x06) | Correto |
| 67 | `0x05` | `bUnitID` | ID da Unidade (5) | Correto |
| 68 | `0x04` | `bSourceID` | ID Origem (4) | Correto |
| 69 | `0x01` | `bControlSize` | Tamanho Controle (1) | Correto |
| 70 | `0x03 0x00` | `bmaControls(0)` | Controles | Correto (ex: Volume/Mute) |
| 72 | `0x00` | `iFeature` | String Index (0) | Correto |

### Áudio Class-Specific Interface (CS_INTERFACE) - OUTPUT_TERMINAL (0x03)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 73 | `0x09` | `bLength` | Tamanho do descritor (9 bytes) | Correto |
| 74 | `0x24` | `bDescriptorType` | CS_INTERFACE (0x24) | Correto |
| 75 | `0x03` | `bDescriptorSubtype` | Subtipo (0x03) | Correto |
| 76 | `0x06` | `bTerminalID` | ID do Terminal (6) | Correto |
| 77 | `0x01 0x01` | `wTerminalType` | Tipo de Terminal (0x0101) | Correto (0x0301 = Speaker, 0x0101 = USB Streaming) |
| 79 | `0x01` | `bAssocTerminal` | Terminal Associado (1) | Correto |
| 80 | `0x05` | `bSourceID` | ID Origem (5) | Correto |
| 81 | `0x00` | `iTerminal` | String Index (0) | Correto |

### Interface 1, Alternate 0 - Áudio Control/Streaming (0x01)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 82 | `0x09` | `bLength` | Tamanho do descritor (9 bytes) | Correto |
| 83 | `0x04` | `bDescriptorType` | Tipo Interface (0x04) | Correto |
| 84 | `0x01` | `bInterfaceNumber` | Número da Interface (1) | Correto (Interface 1) |
| 85 | `0x00` | `bAlternateSetting` | Alternate Setting (0) | Correto |
| 86 | `0x00` | `bNumEndpoints` | Quantidade de Endpoints (0) | Correto |
| 87 | `0x01` | `bInterfaceClass` | Classe (1) | Correto (Áudio Control/Streaming (0x01)) |
| 88 | `0x02` | `bInterfaceSubClass` | Subclasse (2) | Correto |
| 89 | `0x00` | `bInterfaceProtocol` | Protocolo (0) | Correto |
| 90 | `0x00` | `iInterface` | Índice de String (0) | Correto |

### Interface 1, Alternate 1 - Áudio Control/Streaming (0x01)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 91 | `0x09` | `bLength` | Tamanho do descritor (9 bytes) | Correto |
| 92 | `0x04` | `bDescriptorType` | Tipo Interface (0x04) | Correto |
| 93 | `0x01` | `bInterfaceNumber` | Número da Interface (1) | Correto (Interface 1) |
| 94 | `0x01` | `bAlternateSetting` | Alternate Setting (1) | Correto |
| 95 | `0x01` | `bNumEndpoints` | Quantidade de Endpoints (1) | Correto |
| 96 | `0x01` | `bInterfaceClass` | Classe (1) | Correto (Áudio Control/Streaming (0x01)) |
| 97 | `0x02` | `bInterfaceSubClass` | Subclasse (2) | Correto |
| 98 | `0x00` | `bInterfaceProtocol` | Protocolo (0) | Correto |
| 99 | `0x00` | `iInterface` | Índice de String (0) | Correto |

### Áudio Class-Specific Interface (CS_INTERFACE) - HEADER / AS_GENERAL (0x01)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 100 | `0x07` | `bLength` | Tamanho do descritor (7 bytes) | Correto |
| 101 | `0x24` | `bDescriptorType` | CS_INTERFACE (0x24) | Correto |
| 102 | `0x01` | `bDescriptorSubtype` | Subtipo (0x01) | Correto |
| 103 | `0x01` | `bTerminalLink` | Terminal Link (1) | Correto |
| 104 | `0x01` | `bDelay` | Delay (1) | Correto |
| 105 | `0x01 0x00` | `wFormatTag` | Format Tag (0x0001) | Correto (0x0001 = PCM) |

### Áudio Class-Specific Interface (CS_INTERFACE) - INPUT_TERMINAL / FORMAT_TYPE (0x02)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 107 | `0x0B` | `bLength` | Tamanho do descritor (11 bytes) | Correto |
| 108 | `0x24` | `bDescriptorType` | CS_INTERFACE (0x24) | Correto |
| 109 | `0x02` | `bDescriptorSubtype` | Subtipo (0x02) | Correto |
| 110 | `0x01` | `bFormatType` | Tipo Formato (1) | Correto (Tipo I) |
| 111 | `0x04` | `bNrChannels` | Canais (4) | Correto |
| 112 | `0x02` | `bSubframeSize` | Subframe Size (2) | Correto |
| 113 | `0x10` | `bBitResolution` | Resolução (16 bits) | Correto (16 bits) |
| 114 | `0x01` | `bSamFreqType` | Tipo Freq. (1) | Correto |
| 115 | `0x80 0xBB 0x00` | `tSamFreq` | Freq. Amostragem (48000 Hz) | Correto (48000 Hz) |

### Endpoint 1 OUT (Isochronous) (0x01)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 118 | `0x09` | `bLength` | Tamanho do descritor (9 bytes) | Correto |
| 119 | `0x05` | `bDescriptorType` | Tipo Endpoint (0x05) | Correto |
| 120 | `0x01` | `bEndpointAddress` | Endereço (0x01 -> EP1 OUT) | Correto |
| 121 | `0x09` | `bmAttributes` | Atributos (0x09 -> Isochronous) | Correto |
| 122 | `0x88 0x01` | `wMaxPacketSize` | Tamanho do Pacote (392 bytes) | Correto (392 bytes) |
| 124 | `0x01` | `bInterval` | Intervalo de Polling (1) | Correto |
| 125 | `0x00` | `bRefresh` | Refresh (0) | Correto |
| 126 | `0x00` | `bSynchAddress` | Sync Address (0) | Correto |

### Áudio Class-Specific Endpoint (CS_ENDPOINT) - EP_GENERAL (0x01)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 127 | `0x07` | `bLength` | Tamanho do descritor (7 bytes) | Correto |
| 128 | `0x25` | `bDescriptorType` | CS_ENDPOINT (0x25) | Correto |
| 129 | `0x01` | `bDescriptorSubtype` | Subtipo (0x01 - EP_GENERAL) | Correto |
| 130 | `0x00` | `bmAttributes` | Atributos (0x00) | Correto |
| 131 | `0x00` | `bLockDelayUnits` | Lock Delay Units (0) | Correto |
| 132 | `0x00 0x00` | `wLockDelay` | Lock Delay | Correto |

### Interface 2, Alternate 0 - Áudio Control/Streaming (0x01)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 134 | `0x09` | `bLength` | Tamanho do descritor (9 bytes) | Correto |
| 135 | `0x04` | `bDescriptorType` | Tipo Interface (0x04) | Correto |
| 136 | `0x02` | `bInterfaceNumber` | Número da Interface (2) | Correto (Interface 2) |
| 137 | `0x00` | `bAlternateSetting` | Alternate Setting (0) | Correto |
| 138 | `0x00` | `bNumEndpoints` | Quantidade de Endpoints (0) | Correto |
| 139 | `0x01` | `bInterfaceClass` | Classe (1) | Correto (Áudio Control/Streaming (0x01)) |
| 140 | `0x02` | `bInterfaceSubClass` | Subclasse (2) | Correto |
| 141 | `0x00` | `bInterfaceProtocol` | Protocolo (0) | Correto |
| 142 | `0x00` | `iInterface` | Índice de String (0) | Correto |

### Interface 2, Alternate 1 - Áudio Control/Streaming (0x01)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 143 | `0x09` | `bLength` | Tamanho do descritor (9 bytes) | Correto |
| 144 | `0x04` | `bDescriptorType` | Tipo Interface (0x04) | Correto |
| 145 | `0x02` | `bInterfaceNumber` | Número da Interface (2) | Correto (Interface 2) |
| 146 | `0x01` | `bAlternateSetting` | Alternate Setting (1) | Correto |
| 147 | `0x01` | `bNumEndpoints` | Quantidade de Endpoints (1) | Correto |
| 148 | `0x01` | `bInterfaceClass` | Classe (1) | Correto (Áudio Control/Streaming (0x01)) |
| 149 | `0x02` | `bInterfaceSubClass` | Subclasse (2) | Correto |
| 150 | `0x00` | `bInterfaceProtocol` | Protocolo (0) | Correto |
| 151 | `0x00` | `iInterface` | Índice de String (0) | Correto |

### Áudio Class-Specific Interface (CS_INTERFACE) - HEADER / AS_GENERAL (0x01)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 152 | `0x07` | `bLength` | Tamanho do descritor (7 bytes) | Correto |
| 153 | `0x24` | `bDescriptorType` | CS_INTERFACE (0x24) | Correto |
| 154 | `0x01` | `bDescriptorSubtype` | Subtipo (0x01) | Correto |
| 155 | `0x06` | `bTerminalLink` | Terminal Link (6) | Correto |
| 156 | `0x01` | `bDelay` | Delay (1) | Correto |
| 157 | `0x01 0x00` | `wFormatTag` | Format Tag (0x0001) | Correto (0x0001 = PCM) |

### Áudio Class-Specific Interface (CS_INTERFACE) - INPUT_TERMINAL / FORMAT_TYPE (0x02)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 159 | `0x0B` | `bLength` | Tamanho do descritor (11 bytes) | Correto |
| 160 | `0x24` | `bDescriptorType` | CS_INTERFACE (0x24) | Correto |
| 161 | `0x02` | `bDescriptorSubtype` | Subtipo (0x02) | Correto |
| 162 | `0x01` | `bFormatType` | Tipo Formato (1) | Correto (Tipo I) |
| 163 | `0x02` | `bNrChannels` | Canais (2) | Correto |
| 164 | `0x02` | `bSubframeSize` | Subframe Size (2) | Correto |
| 165 | `0x10` | `bBitResolution` | Resolução (16 bits) | Correto (16 bits) |
| 166 | `0x01` | `bSamFreqType` | Tipo Freq. (1) | Correto |
| 167 | `0x80 0xBB 0x00` | `tSamFreq` | Freq. Amostragem (48000 Hz) | Correto (48000 Hz) |

### Endpoint 2 IN (Isochronous) (0x82)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 170 | `0x09` | `bLength` | Tamanho do descritor (9 bytes) | Correto |
| 171 | `0x05` | `bDescriptorType` | Tipo Endpoint (0x05) | Correto |
| 172 | `0x82` | `bEndpointAddress` | Endereço (0x82 -> EP2 IN) | Correto |
| 173 | `0x05` | `bmAttributes` | Atributos (0x05 -> Isochronous) | Correto |
| 174 | `0xC4 0x00` | `wMaxPacketSize` | Tamanho do Pacote (196 bytes) | Correto (196 bytes) |
| 176 | `0x01` | `bInterval` | Intervalo de Polling (1) | Correto |
| 177 | `0x00` | `bRefresh` | Refresh (0) | Correto |
| 178 | `0x00` | `bSynchAddress` | Sync Address (0) | Correto |

### Áudio Class-Specific Endpoint (CS_ENDPOINT) - EP_GENERAL (0x01)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 179 | `0x07` | `bLength` | Tamanho do descritor (7 bytes) | Correto |
| 180 | `0x25` | `bDescriptorType` | CS_ENDPOINT (0x25) | Correto |
| 181 | `0x01` | `bDescriptorSubtype` | Subtipo (0x01 - EP_GENERAL) | Correto |
| 182 | `0x00` | `bmAttributes` | Atributos (0x00) | Correto |
| 183 | `0x00` | `bLockDelayUnits` | Lock Delay Units (0) | Correto |
| 184 | `0x00 0x00` | `wLockDelay` | Lock Delay | Correto |

### Interface 3, Alternate 0 - HID (0x03)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 186 | `0x09` | `bLength` | Tamanho do descritor (9 bytes) | Correto |
| 187 | `0x04` | `bDescriptorType` | Tipo Interface (0x04) | Correto |
| 188 | `0x03` | `bInterfaceNumber` | Número da Interface (3) | Correto (Interface 3) |
| 189 | `0x00` | `bAlternateSetting` | Alternate Setting (0) | Correto |
| 190 | `0x02` | `bNumEndpoints` | Quantidade de Endpoints (2) | Correto |
| 191 | `0x03` | `bInterfaceClass` | Classe (3) | Correto (HID (0x03)) |
| 192 | `0x00` | `bInterfaceSubClass` | Subclasse (0) | Correto |
| 193 | `0x00` | `bInterfaceProtocol` | Protocolo (0) | Correto |
| 194 | `0x00` | `iInterface` | Índice de String (0) | Correto |

### Class-Specific HID
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 195 | `0x09` | `bLength` | Tamanho do descritor (9 bytes) | Correto |
| 196 | `0x21` | `bDescriptorType` | HID (0x21) | Correto |
| 197 | `0x11 0x01` | `bcdHID` | Versão HID (0x0111 -> 1.11) | Correto |
| 199 | `0x00` | `bCountryCode` | Código do País (0) | Correto |
| 200 | `0x01` | `bNumDescriptors` | Descritores de Classe (1) | Correto |
| 201 | `0x22` | `bDescriptorType` | Tipo (0x22 -> Report) | Correto |
| 202 | `0x95 0x01` | `wDescriptorLength` | Tamanho do Report (405 bytes) | **Nota:** O tamanho fixo aqui (405 bytes) está sujeito a **patch dinâmico** durante a inicialização (conforme instrução em memória). Os valores `0x95 0x01` são sobrescritos com o tamanho real do Report Descriptor configurado posteriormente, portanto devem estar nesta exata posição para o código em `usb.cpp` substituí-los corretamente. |

### Endpoint 3 IN (Interrupt) (0x83)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 204 | `0x07` | `bLength` | Tamanho do descritor (7 bytes) | Correto |
| 205 | `0x05` | `bDescriptorType` | Tipo Endpoint (0x05) | Correto |
| 206 | `0x83` | `bEndpointAddress` | Endereço (0x83 -> EP3 IN) | Correto |
| 207 | `0x03` | `bmAttributes` | Atributos (0x03 -> Interrupt) | Correto |
| 208 | `0x40 0x00` | `wMaxPacketSize` | Tamanho do Pacote (64 bytes) | Correto (DualSense HID = 64) |
| 210 | `0x01` | `bInterval` | Intervalo de Polling (1) | Correto |

### Endpoint 4 OUT (Interrupt) (0x04)
| Offset | Byte(s) | Nome do Campo | Descrição | Avaliação/Correção |
|---|---|---|---|---|
| 211 | `0x07` | `bLength` | Tamanho do descritor (7 bytes) | Correto |
| 212 | `0x05` | `bDescriptorType` | Tipo Endpoint (0x05) | Correto |
| 213 | `0x04` | `bEndpointAddress` | Endereço (0x04 -> EP4 OUT) | Correto |
| 214 | `0x03` | `bmAttributes` | Atributos (0x03 -> Interrupt) | Correto |
| 215 | `0x40 0x00` | `wMaxPacketSize` | Tamanho do Pacote (64 bytes) | Correto (DualSense HID = 64) |
| 217 | `0x01` | `bInterval` | Intervalo de Polling (1) | Correto |

## Observações sobre a Compatibilidade com o DualSense Original
- **Interface e Endpoint Setup:** Os descritores recriam fielmente a topologia do DualSense. O dispositivo exibe 4 interfaces (Áudio Control, Áudio Out, Áudio In, HID).
- **Configuração de Pacote (Áudio):** O controle da Sony utiliza `392` bytes no endpoint Áudio OUT e `196` bytes no endpoint Áudio IN para áudio multicanal Isochronous (PCM, 48kHz, 16 bits). Esses tamanhos estão exatamente configurados na estrutura nos offsets 122 e 174, garantindo a reprodução e captação do microfone.
- **Configuração de Pacote (HID):** O polling USB para HID tem relatórios de 64 bytes (`wMaxPacketSize` no Endpoint 3 e 4). Isso obedece ao padrão do controle.
- **Dynamic Descriptor Patching (Tamanho do Report HID):** Como notado no "Class-Specific HID", há uma dependência crítica no tamanho do *Report Descriptor* HID. O arquivo original indica que o byte no offset `202` (código `0x95 0x01` -> 405 bytes) está posicionado ali para ser substituído na inicialização no FunctionFS pela memória da ponte. Está documentado e correto.
