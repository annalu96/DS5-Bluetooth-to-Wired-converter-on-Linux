#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/epoll.h>
#include <csignal>
#include <atomic>
#include <cerrno>

#include "bt.h"
#include "usb.h"
#include "audio.h"

#define MAX_EVENTS 10


std::atomic<bool> daemon_running{true};

void signal_handler(int signum) {
    printf("\nCaught signal %d. Initiating graceful shutdown...\n", signum);
    daemon_running = false;
}

int main() {
    printf("Starting Pico2W DualSense Bridge (Linux Daemon Phase 2 Skeleton)\n");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int bt_fd = bt_init();
    if (bt_fd == -1) {
        printf("Failed to initialize Bluetooth. Exiting.\n");
        return 1;
    }

    audio_init();
    // usb_init(); // To be implemented

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return 1;
    }

    struct epoll_event ev;

    // Add BT socket to epoll
    if (bt_fd != -1) {
        ev.events = EPOLLIN;
        ev.data.fd = bt_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, bt_fd, &ev) == -1) {
            perror("epoll_ctl: bt_fd");
            return 1;
        }
    }

    // TODO (Phase 4):
    // 1. Open USB FunctionFS endpoints (/dev/ffs/ep1, /dev/ffs/ep2, etc.)
    // 2. Add those file descriptors to epoll_fd using epoll_ctl

    struct epoll_event events[MAX_EVENTS];

    printf("Entering main event loop...\n");
    while (daemon_running) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) {
                // Interrupted by signal, just loop again (it will break if daemon_running is false)
                continue;
            }
            perror("epoll_wait");
            break;
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].events & (EPOLLERR | EPOLLHUP)) {
                printf("Error on file descriptor %d\n", events[n].data.fd);
                daemon_running = false;
                break;
            }

            if (events[n].data.fd == bt_fd && (events[n].events & EPOLLIN)) {
                bt_handle_data();
            }

            // TODO (Phase 4):
            // Check which file descriptor is ready and handle reading/writing
            // Example:
            // if (events[n].data.fd == usb_audio_ep) {
            //     int16_t raw[192];
            //     uint32_t bytes = read(usb_audio_ep, raw, sizeof(raw));
            //     audio_receive_pcm(raw, bytes);
            // }
        }
    }

    printf("Cleaning up...\n");
    audio_deinit();
    usb_deinit();
    bt_deinit();
    close(epoll_fd);
    printf("Daemon shut down successfully.\n");
    return 0;
}
