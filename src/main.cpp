#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/epoll.h>

#include "bt.h"
#include "usb.h"
#include "audio.h"

#define MAX_EVENTS 10

int main() {
    printf("Starting Pico2W DualSense Bridge (Linux Daemon Phase 2 Skeleton)\n");

    bt_init();
    audio_init();
    // usb_init(); // To be implemented

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return 1;
    }

    // TODO (Phase 3 & 4):
    // 1. Create Bluetooth RAW HCI socket
    // 2. Open USB FunctionFS endpoints (/dev/ffs/ep1, /dev/ffs/ep2, etc.)
    // 3. Add those file descriptors to epoll_fd using epoll_ctl

    struct epoll_event events[MAX_EVENTS];

    printf("Entering main event loop...\n");
    while (true) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            break;
        }

        for (int n = 0; n < nfds; ++n) {
            // TODO (Phase 3 & 4):
            // Check which file descriptor is ready and handle reading/writing
            // Example:
            // if (events[n].data.fd == bt_socket) { handle_bt_data(); }
            // if (events[n].data.fd == usb_audio_ep) {
            //     int16_t raw[192];
            //     uint32_t bytes = read(usb_audio_ep, raw, sizeof(raw));
            //     audio_receive_pcm(raw, bytes);
            // }
        }
    }

    close(epoll_fd);
    return 0;
}
