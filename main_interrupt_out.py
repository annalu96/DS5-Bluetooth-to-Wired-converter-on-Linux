import re

with open('src/main.cpp', 'r') as f:
    content = f.read()

old_code = """                        uint8_t outputData[78];
                        memset(outputData, 0, sizeof(outputData));

                        outputData[0] = 0xA2; // HID DATA
                        outputData[1] = 0x31; // DualSense Output Report ID
                        outputData[2] = reportSeqCounter << 4;
                        if (++reportSeqCounter == 16) {
                            reportSeqCounter = 0;
                        }
                        outputData[3] = 0x10; // Flags? Usually 0x10 or 0x00
                        memcpy(outputData + 4, buf + 1, ret - 1); // buf[0] is 0x02

                        fill_output_report_checksum(outputData + 1, sizeof(outputData) - 1);

                        bt_write(INTERRUPT, outputData, sizeof(outputData));"""

new_code = """                        uint8_t outputData[78];
                        memset(outputData, 0, sizeof(outputData));

                        outputData[0] = 0xA2; // HID DATA
                        outputData[1] = 0x31; // DualSense Output Report ID
                        outputData[2] = reportSeqCounter << 4;
                        if (++reportSeqCounter == 16) {
                            reportSeqCounter = 0;
                        }
                        outputData[3] = 0x10; // Flags? Usually 0x10 or 0x00
                        // The `ret` from USB EP2 should be 64 bytes (1 byte ID `0x02` + 63 bytes payload).
                        // We copy `ret - 1` (63) bytes to `outputData + 4`.
                        // Then we calculate CRC for the remaining 4 bytes, so total size 1+1+1+1+63+4 = 71 bytes?
                        // Wait, old_main.cpp used `sizeof(outputData)` which is 78 bytes.
                        // The 78 bytes total length is: 0xA2, 0x31, seq, 0x10, then 70 bytes of payload (which includes padding) and 4 CRC.
                        // Let's copy the payload and calculate the CRC on the whole 77 bytes (index 1 to 77).
                        memcpy(outputData + 4, buf + 1, ret - 1);

                        fill_output_report_checksum(outputData + 1, sizeof(outputData) - 1);

                        bt_write(INTERRUPT, outputData, sizeof(outputData));"""

content = content.replace(old_code, new_code)

with open('src/main.cpp', 'w') as f:
    f.write(content)
