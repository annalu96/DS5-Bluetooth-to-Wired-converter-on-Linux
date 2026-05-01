import re

with open('src/usb.cpp', 'r') as f:
    content = f.read()

old_code = """                         if (report_type == 3) { // Feature Report
                             uint8_t final_buf[256];
                             final_buf[0] = 0x53; // 0x53 is SET_REPORT (Feature)
                             final_buf[1] = report_id;
                             memcpy(final_buf + 2, buf + 1, ret - 1);
                             fill_feature_report_checksum(final_buf + 1, ret);
                             bt_write(CONTROL, final_buf, ret + 1);
                         }"""

new_code = """                         if (report_type == 3) { // Feature Report
                             uint8_t final_buf[256];
                             final_buf[0] = 0x53; // 0x53 is SET_REPORT (Feature)
                             final_buf[1] = report_id;
                             memcpy(final_buf + 2, buf + 1, ret - 1);
                             // The total payload is `ret` bytes (including report ID at buf[0]).
                             // We are sending `0x53` (1 byte) + `report_id` (1 byte) + `buf + 1` (ret - 1 bytes) + `CRC` (4 bytes).
                             // So total size is `1 + 1 + ret - 1 + 4` = `ret + 5`.
                             // However, the checksum function expects data starting after `0x53`.
                             // We pass `final_buf + 1` to `fill_feature_report_checksum`, and its length should be `ret + 4` (ID + payload + CRC)
                             fill_feature_report_checksum(final_buf + 1, ret + 4);
                             bt_write(CONTROL, final_buf, ret + 5);
                         }"""

content = content.replace(old_code, new_code)

with open('src/usb.cpp', 'w') as f:
    f.write(content)
