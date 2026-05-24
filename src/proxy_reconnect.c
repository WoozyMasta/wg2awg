#include "proxy_reconnect.h"
#include "log.h"
#include <unistd.h>

int proxy_do_reconnect(proxy_t *p, int (*dial_remote_cb)(proxy_t *, int)) {
    int old_fd = atomic_load_explicit(&p->remote_fd, memory_order_acquire);
    if (old_fd >= 0) {
        close(old_fd);
        atomic_store_explicit(&p->remote_fd, -1, memory_order_release);
    }

    log_info2("reconnecting to ", p->remote_host);

    int fd = dial_remote_cb(p, 1);
    if (fd < 0)
        return -1;

    atomic_store_explicit(&p->last_active, 1, memory_order_relaxed);
    atomic_store_explicit(&p->last_remote_rx, 0, memory_order_relaxed);
    if (p->cfg->mode == AWG_MODE_CLIENT)
        atomic_store_explicit(&p->has_client, 0, memory_order_release);
    atomic_store_explicit(&p->reconnect_needed, 0, memory_order_relaxed);

    atomic_store_explicit(&p->fe_init_seen, 0, memory_order_relaxed);
    atomic_store_explicit(&p->fe_init_sent, 0, memory_order_relaxed);
    atomic_store_explicit(&p->fe_remote_pkt, 0, memory_order_relaxed);
    atomic_store_explicit(&p->fe_resp_received, 0, memory_order_relaxed);
    atomic_store_explicit(&p->fe_resp_sent, 0, memory_order_relaxed);
    atomic_store_explicit(&p->fe_transport_c2s, 0, memory_order_relaxed);
    atomic_store_explicit(&p->fe_transport_s2c, 0, memory_order_relaxed);

    atomic_store_explicit(&p->remote_fd, fd, memory_order_release);
    log_info2("reconnected to ", p->remote_host);
    return fd;
}
