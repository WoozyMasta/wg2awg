#include "proxy.h"
#include "cps.h"
#include "log.h"
#include "net_addr.h"
#include "net_sock.h"
#include "proxy_emit.h"
#include "proxy_io_batch.h"
#include "proxy_c2s_client.h"
#include "proxy_c2s_gateway.h"
#include "proxy_reconnect.h"
#include "proxy_s2c_client.h"
#include "proxy_s2c_gateway.h"
#include "proxy_startup.h"
#include "proxy_control.h"
#include "proxy_shutdown.h"
#include "proxy_init_io.h"
#include "proxy_net.h"
#include "proxy_runtime.h"
#include "obfs.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <pthread.h>

/* ---- Helpers ---- */
static int fill_random_seed(uint64_t *seed) {
    int ufd = open("/dev/urandom", O_RDONLY);
    if (ufd < 0)
        return -1;

    unsigned char *dst = (unsigned char *)seed;
    size_t off = 0;
    while (off < sizeof(*seed)) {
        ssize_t n = read(ufd, dst + off, sizeof(*seed) - off);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        close(ufd);
        return -1;
    }
    close(ufd);
    return 0;
}

static void fill_h4_ring(proxy_t *p) {
    if (p->cfg->h4.min == p->cfg->h4.max) {
        uint32_t v = p->cfg->h4.min;
        for (int i = 0; i < H4_RING_SIZE; i++)
            p->h4_ring[i] = v;
        return;
    }
    for (int i = 0; i < H4_RING_SIZE; i++)
        p->h4_ring[i] = hrange_pick(&p->cfg->h4, fastrand_u64(&p->rng));
}

static inline uint32_t pick_h4(proxy_t *p) {
    uint32_t v = p->h4_ring[p->h4_idx];
    p->h4_idx = (p->h4_idx + 1) & (H4_RING_SIZE - 1);
    if (p->h4_idx == 0)
        fill_h4_ring(p);
    return v;
}

static int checked_mul_size(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a)
        return -1;
    *out = a * b;
    return 0;
}

static int junk_layout_sizes(const awg_config_t *cfg, size_t *junk_bytes,
                             size_t *junk_sizes_bytes) {
    if (checked_mul_size((size_t)cfg->jc, (size_t)cfg->jmax, junk_bytes) < 0)
        return -1;
    if (checked_mul_size((size_t)cfg->jc, sizeof(int), junk_sizes_bytes) < 0)
        return -1;
    return 0;
}

/* ---- GRO/GSO ---- */

/* ---- Init ---- */

int proxy_init(proxy_t *p, awg_config_t *cfg, const char *listen_str,
               const char *remote_str, int src_port) {
    const char *cfg_err = NULL;
    memset(p, 0, sizeof(*p));
    p->cfg = cfg;
    p->listen_fd = -1;
    atomic_store_explicit(&p->remote_fd, -1, memory_order_relaxed);
    p->signal_fd = -1;
    p->timer_fd = -1;
    p->gso_ok = 1;

    if (config_validate(cfg, &cfg_err) < 0) {
        log_error2("invalid config: ", cfg_err);
        return -1;
    }

    /* Parse listen address */
    char host[256];
    uint16_t port;
    if (net_addr_parse_host_port(listen_str, host, sizeof(host), &port) < 0)
        return -1;
    if (net_addr_resolve_host_port(host, port, 1, &p->listen_addr,
                                   &p->listen_addr_len) < 0)
        return -1;

    /* Parse remote address */
    if (net_addr_parse_host_port(remote_str, p->remote_host,
                                 sizeof(p->remote_host), &p->remote_port) < 0)
        return -1;

    if (src_port > 0) {
        p->local_port = src_port;
    } else {
        p->auto_src_port = 1;
    }

    /* Init PRNG */
    uint64_t seed;
    if (fill_random_seed(&seed) < 0) {
        seed = (uint64_t)(uintptr_t)p ^ 0xDEADBEEFCAFEULL;
    }
    fastrand_init(&p->rng, seed);
    obfs_session_init(&p->obfs_c2s, cfg->obfs_profile,
                      seed ^ 0x1111111111111111ULL);
    obfs_session_init(&p->obfs_s2c, cfg->obfs_profile,
                      seed ^ 0x2222222222222222ULL);

    /* Pre-allocate junk buffers */
    if (cfg->jc > 0 && cfg->jmax > 0) {
        size_t junk_bytes;
        size_t junk_sizes_bytes;
        if (junk_layout_sizes(cfg, &junk_bytes, &junk_sizes_bytes) < 0)
            return -1;
        p->junk_buf = (uint8_t *)malloc(junk_bytes);
        p->junk_sizes = (int *)malloc(junk_sizes_bytes);
        if (!p->junk_buf || !p->junk_sizes)
            return -1;
    }

    /* Init H4 ring */
    fill_h4_ring(p);

    proxy_init_io_state(p);

    return 0;
}

/* ---- Send helpers ---- */

/* ---- Send batch with GSO ---- */

/* ---- c2s thread ---- */

static void *c2s_thread(void *arg) {
    proxy_t *p = (proxy_t *)arg;
    proxy_set_thread_affinity(p->cfg->cpu_c2s, "c2s");
    if (p->cfg->mode != AWG_MODE_CLIENT)
        return proxy_c2s_thread_gateway(arg, proxy_log_addr);
    return proxy_c2s_thread_client(arg, pick_h4, proxy_log_addr);
}

/* ---- s2c thread ---- */

/* ---- s2c packet processing: client mode (AWG->WG inbound) ---- */

/* ---- s2c packet processing: gateway/server mode (WG->AWG outbound) ---- */

__attribute__((hot)) static void *s2c_thread(void *arg) {
    proxy_t *p = (proxy_t *)arg;
    proxy_set_thread_affinity(p->cfg->cpu_s2c, "s2c");
    int reconnect_backoff = 1;
    int prev_nrecv = BATCH_SIZE;

    /* Try to enable GRO on initial remote fd */
    int remote_fd = atomic_load_explicit(&p->remote_fd, memory_order_acquire);
    if (remote_fd >= 0 && !p->cfg->no_gro) {
        p->gro_enabled = proxy_io_enable_gro(remote_fd);
        if (p->gro_enabled)
            log_info("s2c: UDP GRO enabled");
    }
    if (p->cfg->no_gro)
        log_info("s2c: UDP GRO disabled (AWG_NO_GRO)");

    int gateway_path = (p->cfg->mode != AWG_MODE_CLIENT);
    int s2c_headroom = gateway_path ? p->cfg->s4 : 0;
    int s2c_buflen = BUF_SIZE + AWG_PACKET_HEADROOM - s2c_headroom;
    int gro_no_coalesce = 0;

    while (!atomic_load_explicit(&p->stopped, memory_order_relaxed)) {
        remote_fd = atomic_load_explicit(&p->remote_fd, memory_order_acquire);

        /* Reconnect if needed */
        if (remote_fd < 0 ||
            atomic_load_explicit(&p->reconnect_needed, memory_order_relaxed)) {
            struct timespec slp = {.tv_sec = reconnect_backoff};
            nanosleep(&slp, NULL);
            if (atomic_load_explicit(&p->stopped, memory_order_relaxed))
                break;

            int new_fd = proxy_do_reconnect(p, proxy_dial_remote);
            if (new_fd < 0) {
                reconnect_backoff *= 2;
                if (reconnect_backoff > 30)
                    reconnect_backoff = 30;
                log_error("reconnect failed, backing off");
                continue;
            }
            reconnect_backoff = 1;
            remote_fd = new_fd;
            /* Re-enable GRO on new fd */
            if (!p->cfg->no_gro) {
                p->gro_enabled = proxy_io_enable_gro(remote_fd);
                if (p->gro_enabled)
                    log_info("s2c: UDP GRO re-enabled");
            }
            prev_nrecv = BATCH_SIZE;
            continue;
        }

        /* === Receive === */
        int nsend = 0;

        if (p->gro_enabled && !gateway_path) {
            /* GRO path: client mode only (gateway uses outbound which needs
             * headroom) */
            int seg_size;
            int n = proxy_io_recv_gro(p, remote_fd, &seg_size);
            if (n <= 0) {
                int saved_errno = errno;
                if (n == 0 ||
                    (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK &&
                     saved_errno != EINTR)) {
                    if (!atomic_load_explicit(&p->stopped,
                                              memory_order_relaxed)) {
                        log_info3("remote read error (", strerror(saved_errno),
                                  "), will reconnect");
                        atomic_store_explicit(&p->reconnect_needed, 1,
                                              memory_order_relaxed);
                    }
                }
                continue;
            }

            atomic_store_explicit(&p->last_active, 1, memory_order_relaxed);
            atomic_store_explicit(&p->last_remote_rx, 1, memory_order_relaxed);
            reconnect_backoff = 1;

            if (!atomic_exchange_explicit(&p->fe_remote_pkt, 1,
                                          memory_order_relaxed)) {
                char nb[12];
                const char *parts[] = {
                    "s2c: first packet received from remote (size=",
                    u32_to_str(nb, n), ")"};
                log_infon(parts, 3);
            }

            if (!atomic_load_explicit(&p->has_client, memory_order_acquire))
                continue;

            if (seg_size > 0 && n > seg_size) {
                gro_no_coalesce = 0;
                char nb[12], sb[12];
                log_debug3("s2c: GRO recv bytes=", u32_to_str(nb, n),
                           u32_to_str(sb, seg_size));

                /* Coalesced: split buffer by seg_size */
                for (int off = 0; off < n && nsend < BATCH_SIZE;
                     off += seg_size) {
                    int end = off + seg_size;
                    if (end > n)
                        end = n;
                    int pkt_len = end - off;
                    proxy_s2c_process_client(
                        p, p->gro_buf + off, pkt_len, p->send_s2c.iovecs,
                        p->send_s2c.addrs, p->send_s2c.addrlens, &nsend);
                }
            } else {
                /* Single packet - GRO not coalescing */
                if (++gro_no_coalesce >= 8) {
                    p->gro_enabled = 0;
                    log_info("GRO not coalescing, falling back to recvmmsg");
                }
                proxy_s2c_process_client(p, p->gro_buf, n, p->send_s2c.iovecs,
                                         p->send_s2c.addrs,
                                         p->send_s2c.addrlens, &nsend);
            }
        } else {
            /* Non-GRO path (or gateway mode): recvmmsg with MSG_WAITFORONE */
            for (int i = 0; i < prev_nrecv; i++)
                p->recv_s2c.iovecs[i].iov_len = s2c_buflen;

            int nrecv = recvmmsg(remote_fd, p->recv_s2c.msgs, BATCH_SIZE,
                                 MSG_WAITFORONE, NULL);
            if (nrecv <= 0) {
                int saved_errno = errno;
                if (nrecv == 0 ||
                    (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK &&
                     saved_errno != EINTR)) {
                    if (!atomic_load_explicit(&p->stopped,
                                              memory_order_relaxed)) {
                        log_info3("remote read error (", strerror(saved_errno),
                                  "), will reconnect");
                        atomic_store_explicit(&p->reconnect_needed, 1,
                                              memory_order_relaxed);
                    }
                }
                continue;
            }
            prev_nrecv = nrecv;

            atomic_store_explicit(&p->last_active, 1, memory_order_relaxed);
            atomic_store_explicit(&p->last_remote_rx, 1, memory_order_relaxed);
            reconnect_backoff = 1;

            if (!atomic_exchange_explicit(&p->fe_remote_pkt, 1,
                                          memory_order_relaxed)) {
                int first_n = (int)p->recv_s2c.msgs[0].msg_len;
                char nb[12];
                const char *parts[] = {
                    "s2c: first packet received from remote (size=",
                    u32_to_str(nb, first_n), ")"};
                log_infon(parts, 3);
            }

            if (!gateway_path &&
                !atomic_load_explicit(&p->has_client, memory_order_acquire))
                continue;

            for (int i = 0; i < nrecv; i++) {
                int n = (int)p->recv_s2c.msgs[i].msg_len;
                if (n <= 0)
                    continue;
                if (gateway_path) {
                    proxy_s2c_process_gateway(
                        p, p->recv_s2c.bufs[i],
                        p->recv_s2c.bufs[i] + s2c_headroom, n, s2c_headroom,
                        p->send_s2c.iovecs, p->send_s2c.addrs,
                        p->send_s2c.addrlens, &nsend, pick_h4);
                } else {
                    proxy_s2c_process_client(
                        p, p->recv_s2c.bufs[i], n, p->send_s2c.iovecs,
                        p->send_s2c.addrs, p->send_s2c.addrlens, &nsend);
                }
            }
        }

        /* === Send === */
        if (nsend > 0) {
            for (int i = 0; i < nsend; i++)
                p->send_s2c.msgs[i].msg_hdr.msg_namelen =
                    p->send_s2c.addrlens[i];
            const struct sockaddr_storage *gso_addr =
                (p->cfg->mode == AWG_MODE_SERVER) ? NULL
                                                  : &p->send_s2c.addrs[0];
            socklen_t gso_addr_len =
                (p->cfg->mode == AWG_MODE_SERVER) ? 0 : p->send_s2c.addrlens[0];
            proxy_io_send_batch_gso(p, p->listen_fd, p->send_s2c.msgs,
                                    p->send_s2c.iovecs, nsend, gso_addr,
                                    gso_addr_len);
        }
    }

    return NULL;
}

/* ---- Main ---- */

int proxy_run(proxy_t *p) {
    awg_config_t *cfg = p->cfg;
    int rc = -1;
    pthread_t t_c2s;
    pthread_t t_s2c;
    int c2s_started = 0;
    int s2c_started = 0;

    if (proxy_startup_network(p, proxy_dial_remote, proxy_log_socket_buffers) <
        0)
        goto cleanup;

    if (proxy_control_init(p) < 0)
        goto cleanup;

    /* Launch c2s and s2c threads */
    if (pthread_create(&t_c2s, NULL, c2s_thread, p) != 0) {
        log_error("pthread_create(c2s) failed");
        goto cleanup;
    }
    c2s_started = 1;
    if (pthread_create(&t_s2c, NULL, s2c_thread, p) != 0) {
        log_error("pthread_create(s2c) failed");
        goto stop_threads;
    }
    s2c_started = 1;

    int timeout_secs = cfg->timeout > 0 ? cfg->timeout : 180;
    int silent_secs =
        cfg->remote_silent_timeout > 0 ? cfg->remote_silent_timeout : 300;
    proxy_control_loop(p, timeout_secs, silent_secs);

stop_threads:
    proxy_shutdown_sockets(p);
    if (c2s_started)
        pthread_join(t_c2s, NULL);
    if (s2c_started)
        pthread_join(t_s2c, NULL);
    rc = 0;

cleanup:
    proxy_cleanup_resources(p);
    return rc;
}
