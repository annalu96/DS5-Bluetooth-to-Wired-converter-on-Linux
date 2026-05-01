with open('src/usb.h', 'r') as f:
    content = f.read()

content = content.replace('extern int ep1_fd;\nextern int ep2_fd;\nextern int ep3_fd;', '''extern int ep_audio_out_fd;
extern int ep_audio_in_fd;
extern int ep_hid_in_fd;
extern int ep_hid_out_fd;''')

with open('src/usb.h', 'w') as f:
    f.write(content)
