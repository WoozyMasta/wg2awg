#include "proxy_c2s_gateway.h"
#include "proxy_io_batch.h"
#include "log.h"
#include "obfs.h"
#include <string.h>

static int should_log_obfs_fail(uint32_t c) {
    return c == 1 || (c >= 8 && (c & (c - 1u)) == 0);
}

static void log_marker_accept_once(obfs_session_t *s, const char *side) {
    static _Thread_local int logged = 0;
    if (!logged && s->profile != AWG_OBFS_OFF && s->marker_seen) {
        const char *parts[] = {side, ": obfs marker accepted"};
        log_infon(parts, 2);
        logged = 1;
    }
}

static void log_marker_reject_once(obfs_session_t *s, const char *side) {
    static _Thread_local int logged = 0;
    if (!logged && s->profile != AWG_OBFS_OFF && !s->marker_seen &&
        s->marker_rx_window == 0) {
        const char *parts[] = {side, ": obfs marker rejected"};
        log_msgn("ERROR: ", parts, 2);
        logged = 1;
    }
}

void *proxy_c2s_thread_gateway(
    void *arg,
    void (*log_addr_cb)(const char *, const struct sockaddr_storage *)) {
    proxy_t *p = (proxy_t *)arg;
    awg_config_t *cfg = p->cfg;
    int server_mode = (cfg->mode == AWG_MODE_SERVER);
    int s4 = cfg->s4;
    int c2s_buflen = BUF_SIZE + AWG_PACKET_HEADROOM;
    int prev_nrecv = BATCH_SIZE;

    while (!atomic_load_explicit(&p->stopped, memory_order_relaxed)) {
        for (int i = 0; i < prev_nrecv; i++) {
            p->recv_c2s.iovecs[i].iov_len = c2s_buflen;
            p->recv_c2s.msgs[i].msg_hdr.msg_namelen =
                sizeof(struct sockaddr_storage);
        }

        int nrecv = recvmmsg(p->listen_fd, p->recv_c2s.msgs, BATCH_SIZE,
                             MSG_WAITFORONE, NULL);
        if (nrecv <= 0) {
            if (atomic_load_explicit(&p->stopped, memory_order_relaxed))
                break;
            continue;
        }
        prev_nrecv = nrecv;

        atomic_store_explicit(&p->last_active, 1, memory_order_relaxed);

        if (!server_mode && (p->recv_c2s.addrs[0].ss_family == AF_INET ||
                             p->recv_c2s.addrs[0].ss_family == AF_INET6)) {
            socklen_t recv_addr_len = p->recv_c2s.msgs[0].msg_hdr.msg_namelen;
            if (recv_addr_len > (socklen_t)sizeof(p->recv_c2s.addrs[0]))
                recv_addr_len = (socklen_t)sizeof(p->recv_c2s.addrs[0]);
            if (!atomic_load_explicit(&p->has_client, memory_order_acquire) ||
                !session_addr_equal(&p->client_addr, p->client_addr_len,
                                    &p->recv_c2s.addrs[0], recv_addr_len)) {
                session_addr_copy(&p->client_addr, &p->client_addr_len,
                                  &p->recv_c2s.addrs[0], recv_addr_len);
                atomic_store_explicit(&p->has_client, 1, memory_order_release);
                log_addr_cb("client: ", &p->client_addr);
            }
        }

        int remote_fd =
            atomic_load_explicit(&p->remote_fd, memory_order_acquire);
        if (remote_fd < 0)
            continue;

        int nsend = 0;
        for (int i = 0; i < nrecv; i++) {
            int n = (int)p->recv_c2s.msgs[i].msg_len;
            if (n <= 0)
                continue;

            uint8_t *pkt = p->recv_c2s.bufs[i];
            obfs_session_t *obfs_rx = &p->obfs_c2s;
            if (server_mode) {
                socklen_t addr_len = p->recv_c2s.msgs[i].msg_hdr.msg_namelen;
                if (addr_len > (socklen_t)sizeof(p->recv_c2s.addrs[i]))
                    addr_len = (socklen_t)sizeof(p->recv_c2s.addrs[i]);
                session_entry_t *entry = session_find_by_addr(
                    p->sessions, &p->recv_c2s.addrs[i], addr_len);
                if (entry && !atomic_load_explicit(&entry->obfs_ready,
                                                   memory_order_relaxed)) {
                    obfs_session_init(&entry->obfs_c2s, cfg->obfs_profile,
                                      fastrand_u64(&p->rng));
                    obfs_session_init(&entry->obfs_s2c, cfg->obfs_profile,
                                      fastrand_u64(&p->rng));
                    atomic_store_explicit(&entry->obfs_ready, 1,
                                          memory_order_relaxed);
                }
                if (entry)
                    obfs_rx = &entry->obfs_c2s;
            }

            if (n >= s4 + WG_TRANSPORT_MIN) {
                if (!cfg->transport_size_ambiguous ||
                    (n != cfg->init_total && n != cfg->resp_total &&
                     n != cfg->cookie_total)) {
                    uint32_t h;
                    memcpy(&h, pkt + s4, 4);
                    if (hrange_contains(&cfg->h4, h)) {
                        uint32_t wt = WG_TRANSPORT_DATA;
                        memcpy(pkt + s4, &wt, 4);
                        p->send_c2s.iovecs[nsend].iov_base = pkt + s4;
                        p->send_c2s.iovecs[nsend].iov_len = n - s4;
                        nsend++;
                        continue;
                    }
                }
            }

            if (nsend > 0) {
                proxy_io_send_batch_gso(p, remote_fd, p->send_c2s.msgs,
                                        p->send_c2s.iovecs, nsend, NULL, 0);
                nsend = 0;
            }

            int unwrapped_len = 0;
            int marker_seen_before = obfs_rx->marker_seen;
            uint8_t *unwrapped = obfs_unwrap(obfs_rx, pkt, n, &unwrapped_len);
            if (!unwrapped) {
                if (!marker_seen_before)
                    log_marker_reject_once(obfs_rx, "c2s");
                p->obfs_fail_c2s++;
                if (should_log_obfs_fail(p->obfs_fail_c2s)) {
                    char cb[12];
                    const char *parts[] = {
                        "c2s: obfs unwrap failures=",
                        u32_to_str(cb, p->obfs_fail_c2s),
                        " profile=", obfs_profile_name(cfg->obfs_profile),
                        " (remote likely plain AWG/WG or different obfs "
                        "preset)"};
                    log_msgn("ERROR: ", parts, 6);
                }
                continue;
            }
            if (!marker_seen_before)
                log_marker_accept_once(obfs_rx, "c2s");

            int out_len;
            uint8_t *out =
                transform_inbound(unwrapped, unwrapped_len, cfg, &out_len);
            if (!out)
                continue;

            if (server_mode && (p->recv_c2s.addrs[i].ss_family == AF_INET ||
                                p->recv_c2s.addrs[i].ss_family == AF_INET6)) {
                uint32_t msg_type;
                socklen_t addr_len = p->recv_c2s.msgs[i].msg_hdr.msg_namelen;
                if (addr_len > (socklen_t)sizeof(p->recv_c2s.addrs[i]))
                    addr_len = (socklen_t)sizeof(p->recv_c2s.addrs[i]);
                memcpy(&msg_type, out, 4);
                if (msg_type == WG_HANDSHAKE_INIT && out_len == WG_INIT_SIZE) {
                    uint32_t sender_idx;
                    memcpy(&sender_idx, out + 4, 4);
                    session_put(p->sessions, sender_idx, &p->recv_c2s.addrs[i],
                                addr_len);
                    session_entry_t *entry =
                        session_get_entry(p->sessions, sender_idx);
                    if (entry && !atomic_load_explicit(&entry->obfs_ready,
                                                       memory_order_relaxed)) {
                        obfs_session_init(&entry->obfs_c2s, cfg->obfs_profile,
                                          fastrand_u64(&p->rng));
                        obfs_session_init(&entry->obfs_s2c, cfg->obfs_profile,
                                          fastrand_u64(&p->rng));
                        atomic_store_explicit(&entry->obfs_ready, 1,
                                              memory_order_relaxed);
                    }
                    log_debug("c2s: server: recorded init sender_index");
                } else if (msg_type == WG_HANDSHAKE_RESPONSE &&
                           out_len == WG_RESP_SIZE) {
                    uint32_t sender_idx;
                    memcpy(&sender_idx, out + 4, 4);
                    session_put(p->sessions, sender_idx, &p->recv_c2s.addrs[i],
                                addr_len);
                    session_entry_t *entry =
                        session_get_entry(p->sessions, sender_idx);
                    if (entry && !atomic_load_explicit(&entry->obfs_ready,
                                                       memory_order_relaxed)) {
                        obfs_session_init(&entry->obfs_c2s, cfg->obfs_profile,
                                          fastrand_u64(&p->rng));
                        obfs_session_init(&entry->obfs_s2c, cfg->obfs_profile,
                                          fastrand_u64(&p->rng));
                        atomic_store_explicit(&entry->obfs_ready, 1,
                                              memory_order_relaxed);
                    }
                    log_debug("c2s: server: recorded response sender_index");
                }
            }

            p->send_c2s.iovecs[nsend].iov_base = out;
            p->send_c2s.iovecs[nsend].iov_len = out_len;
            nsend++;
        }

        if (nsend > 0) {
            proxy_io_send_batch_gso(p, remote_fd, p->send_c2s.msgs,
                                    p->send_c2s.iovecs, nsend, NULL, 0);
        }
    }

    return NULL;
}
