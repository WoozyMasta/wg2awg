#include "proxy_startup.h"
#include "net_sock.h"
#include "proxy_io_batch.h"
#include "log.h"
#include <unistd.h>
#include <time.h>

int proxy_startup_network(proxy_t *p, int (*dial_remote_cb)(proxy_t *, int),
                          void (*log_socket_buffers_cb)(int,
                                                        const awg_config_t *,
                                                        const char *)) {
    awg_config_t *cfg = p->cfg;

    p->listen_fd = net_sock_create_udp(p->listen_addr.ss_family, 1);
    if (p->listen_fd < 0) {
        log_error("socket create failed");
        return -1;
    }
    if (net_sock_set_reuseaddr(p->listen_fd) < 0 ||
        net_sock_bind(p->listen_fd, &p->listen_addr, p->listen_addr_len) < 0) {
        log_error("bind failed");
        close(p->listen_fd);
        return -1;
    }
    net_sock_set_buffers(p->listen_fd, cfg->socket_buf);
    net_sock_set_busy_poll(p->listen_fd, cfg->busy_poll, BATCH_SIZE);
    if (!cfg->no_gro) {
        p->gro_enabled_c2s = proxy_io_enable_gro(p->listen_fd);
        if (p->gro_enabled_c2s)
            log_info("c2s: UDP GRO enabled");
    }
    log_socket_buffers_cb(p->listen_fd, cfg, "listen");

    int rfd = -1;
    int max_retries = p->cfg->connect_retries;
    int attempt = 0;
    for (;;) {
        rfd = dial_remote_cb(p, 1);
        if (rfd >= 0)
            break;
        attempt++;
        if (max_retries > 0 && attempt >= max_retries) {
            char ab[12];
            const char *eparts[] = {"initial connect failed after ",
                                    u32_to_str(ab, attempt), " attempts"};
            if (g_log_level >= LOG_ERROR)
                log_msgn("ERROR: ", eparts, 3);
            close(p->listen_fd);
            return -1;
        }
        int delay = 1;
        for (int i = 1; i < attempt && delay < 60; i++)
            delay *= 2;
        if (delay > 60)
            delay = 60;
        char db[12];
        log_error2("initial connect failed, retrying in ",
                   u32_to_str(db, delay));
        struct timespec slp = {.tv_sec = delay};
        nanosleep(&slp, NULL);
    }

    atomic_store_explicit(&p->remote_fd, rfd, memory_order_release);
    log_socket_buffers_cb(rfd, cfg, "remote");
    atomic_store_explicit(&p->last_active, 1, memory_order_relaxed);
    return 0;
}
