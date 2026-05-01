with open('src/main.cpp', 'r') as f:
    content = f.read()

content = content.replace('ep1_fd', 'ep_hid_in_fd')
content = content.replace('ep2_fd', 'ep_hid_out_fd')

# Now add ep_audio_out_fd to epoll and handle it
ep2_add = """    if (ep_hid_out_fd != -1) {
        ev.events = EPOLLIN;
        ev.data.fd = ep_hid_out_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ep_hid_out_fd, &ev) == -1) {
            perror("epoll_ctl: ep_hid_out_fd");
            return 1;
        }
    }"""

audio_add = """    // Add USB EP1 (Audio OUT) to epoll
    if (ep_audio_out_fd != -1) {
        ev.events = EPOLLIN;
        ev.data.fd = ep_audio_out_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ep_audio_out_fd, &ev) == -1) {
            perror("epoll_ctl: ep_audio_out_fd");
            return 1;
        }
    }"""

content = content.replace(ep2_add, ep2_add + '\n\n' + audio_add)

ep2_handle = """            else if (fd == ep_hid_out_fd && (events[n].events & EPOLLIN)) {
                // USB OUT endpoint received data (from Steam/Proton)
                uint8_t buf[64];
                int ret = read(ep_hid_out_fd, buf, sizeof(buf));"""

audio_handle = """            else if (fd == ep_audio_out_fd && (events[n].events & EPOLLIN)) {
                int16_t buf[196]; // 392 bytes max packet size
                int ret = read(ep_audio_out_fd, buf, sizeof(buf));
                if (ret > 0) {
                    audio_receive_pcm(buf, ret);
                }
            }
"""

content = content.replace(ep2_handle, audio_handle + ep2_handle)

with open('src/main.cpp', 'w') as f:
    f.write(content)
