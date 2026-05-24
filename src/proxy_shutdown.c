#include "proxy_shutdown.h"
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>

void proxy_shutdown_sockets(proxy_t *p) {
    atomic_store_explicit(&p->stopped, 1, memory_order_relaxed);
    if (p->listen_fd >= 0)
        shutdown(p->listen_fd, SHUT_RDWR);
    int rfd = atomic_load_explicit(&p->remote_fd, memory_order_acquire);
    if (rfd >= 0)
        shutdown(rfd, SHUT_RDWR);
}

void proxy_join_threads(pthread_t t_c2s, pthread_t t_s2c) {
    pthread_join(t_c2s, NULL);
    pthread_join(t_s2c, NULL);
}

void proxy_cleanup_resources(proxy_t *p) {
    int rfd = atomic_load_explicit(&p->remote_fd, memory_order_relaxed);
    if (rfd >= 0) {
        close(rfd);
        atomic_store_explicit(&p->remote_fd, -1, memory_order_relaxed);
    }
    if (p->listen_fd >= 0) {
        close(p->listen_fd);
        p->listen_fd = -1;
    }
    if (p->signal_fd >= 0) {
        close(p->signal_fd);
        p->signal_fd = -1;
    }
    if (p->timer_fd >= 0) {
        close(p->timer_fd);
        p->timer_fd = -1;
    }
    free(p->junk_buf);
    p->junk_buf = NULL;
    free(p->junk_sizes);
    p->junk_sizes = NULL;
}
