import re

with open('src/usb.cpp', 'r') as f:
    content = f.read()

old_code = """                 if (report_type == 3) { // Feature Report
                     uint8_t buf[64];
                     memset(buf, 0, sizeof(buf));
                     buf[0] = report_id;

                     if (setup.wLength <= sizeof(buf)) {
                         write(ep0_fd, buf, setup.wLength);

                         uint8_t get_feature[2] = {0x43, report_id};
                         bt_write(CONTROL, get_feature, sizeof(get_feature));
                     } else {
                         write(ep0_fd, buf, sizeof(buf));
                     }"""

new_code = """                 if (report_type == 3) { // Feature Report
                     std::vector<uint8_t> cached_data;
                     if (feature_data.find(report_id) != feature_data.end()) {
                         cached_data = feature_data[report_id];
                     }

                     if (feature_data.find(report_id) == feature_data.end() || report_id == 0x81) {
                         uint8_t get_feature[2] = {0x43, report_id};
                         bt_write(CONTROL, get_feature, sizeof(get_feature));
                     }

                     uint8_t buf[256];
                     memset(buf, 0, sizeof(buf));

                     if (!cached_data.empty()) {
                         size_t copy_len = std::min((size_t)setup.wLength, cached_data.size() - 1);
                         memcpy(buf, cached_data.data() + 1, copy_len);
                     } else {
                         buf[0] = report_id;
                     }

                     if (setup.wLength <= sizeof(buf)) {
                         write(ep0_fd, buf, setup.wLength);
                     } else {
                         write(ep0_fd, buf, sizeof(buf));
                     }"""

content = content.replace(old_code, new_code)

with open('src/usb.cpp', 'w') as f:
    f.write(content)
