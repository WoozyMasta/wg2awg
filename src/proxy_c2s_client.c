#include "proxy_c2s_client.h"
#include "proxy_emit.h"
#include "proxy_io_batch.h"
#include "log.h"
#include "net_addr.h"
#include "obfs.h"
#include <string.h>

static int obfs_max_packet_len(proxy_t *p) {
    return BUF_SIZE + AWG_PACKET_HEADROOM -
           obfs_profile_overhead_max(p->cfg->obfs_profile);
}

static void log_marker_sent_once(obfs_session_t *s, const char *side) {
    static _Thread_local int logged = 0;
    if (!logged && s->profile != AWG_OBFS_OFF && s->marker_tx_left < 3) {
        const char *parts[] = {side, ": obfs marker sent"};
        log_infon(parts, 2);
        logged = 1;
    }
}

void *proxy_c2s_thread_client(
    void *arg, uint32_t (*pick_h4_cb)(proxy_t *),
    void (*log_addr_cb)(const char *, const struct sockaddr_storage *)) {
    proxy_t *p = (proxy_t *)arg;
    awg_config_t *cfg = p->cfg;
    int prefix = cfg->s4;
    int prev_nrecv = BATCH_SIZE;
    int gro_no_coalesce = 0;
    int max_obfs_in = obfs_max_packet_len(p);

    while (!atomic_load_explicit(&p->stopped, memory_order_relaxed)) {
        int nrecv;

        if (p->gro_enabled_c2s) {
            p->gro_hdr_c2s.msg_controllen = sizeof(p->gro_cmsg_c2s);
            p->gro_hdr_c2s.msg_flags = 0;
            p->gro_hdr_c2s.msg_namelen = sizeof(struct sockaddr_storage);

            ssize_t total = recvmsg(p->listen_fd, &p->gro_hdr_c2s, 0);
            if (total <= 0) {
                if (atomic_load_explicit(&p->stopped, memory_order_relaxed))
                    break;
                continue;
            }

            int seg_size = 0;
            for (struct cmsghdr *cm = CMSG_FIRSTHDR(&p->gro_hdr_c2s); cm;
                 cm = CMSG_NXTHDR(&p->gro_hdr_c2s, cm)) {
                if (cm->cmsg_level == IPPROTO_UDP &&
                    cm->cmsg_type == UDP_SEGMENT) {
                    uint16_t ss;
                    memcpy(&ss, CMSG_DATA(cm), sizeof(ss));
                    seg_size = ss;
                    break;
                }
            }

            p->recv_c2s.addrs[0] = p->gro_addr_c2s;
            nrecv = 0;

            if (seg_size > 0 && total > seg_size) {
                gro_no_coalesce = 0;
                for (int off = 0; off < total && nrecv < BATCH_SIZE;
                     off += seg_size) {
                    int plen = (off + seg_size <= total) ? seg_size
                                                         : (int)(total - off);
                    if (plen > BUF_SIZE) {
                        log_debug("c2s: dropping oversized GRO segment");
                        continue;
                    }
                    memcpy(p->recv_c2s.bufs[nrecv] + prefix,
                           p->gro_buf_c2s + off, plen);
                    p->recv_c2s.msgs[nrecv].msg_len = plen;
                    nrecv++;
                }
            } else {
                if (++gro_no_coalesce >= 8) {
                    p->gro_enabled_c2s = 0;
                    log_info(
                        "c2s: GRO not coalescing, falling back to recvmmsg");
                }
                if (total > BUF_SIZE) {
                    log_debug("c2s: dropping oversized GRO datagram");
                    continue;
                }
                memcpy(p->recv_c2s.bufs[0] + prefix, p->gro_buf_c2s,
                       (int)total);
                p->recv_c2s.msgs[0].msg_len = (unsigned int)total;
                nrecv = 1;
            }
        } else {
            for (int i = 0; i < prev_nrecv; i++)
                p->recv_c2s.iovecs[i].iov_len = BUF_SIZE;
            p->recv_c2s.msgs[0].msg_hdr.msg_namelen =
                sizeof(struct sockaddr_storage);

            nrecv = recvmmsg(p->listen_fd, p->recv_c2s.msgs, BATCH_SIZE,
                             MSG_WAITFORONE, NULL);
            if (nrecv <= 0) {
                if (atomic_load_explicit(&p->stopped, memory_order_relaxed))
                    break;
                continue;
            }
            prev_nrecv = nrecv;
        }

        atomic_store_explicit(&p->last_active, 1, memory_order_relaxed);

        if (p->recv_c2s.addrs[0].ss_family == AF_INET ||
            p->recv_c2s.addrs[0].ss_family == AF_INET6) {
            socklen_t recv_addr_len =
                p->gro_enabled_c2s ? p->gro_hdr_c2s.msg_namelen
                                   : p->recv_c2s.msgs[0].msg_hdr.msg_namelen;
            if (recv_addr_len > (socklen_t)sizeof(p->recv_c2s.addrs[0]))
                recv_addr_len = (socklen_t)sizeof(p->recv_c2s.addrs[0]);
            if (!atomic_load_explicit(&p->has_client, memory_order_acquire) ||
                !session_addr_equal(&p->client_addr, p->client_addr_len,
                                    &p->recv_c2s.addrs[0], recv_addr_len)) {
                session_addr_copy(&p->client_addr, &p->client_addr_len,
                                  &p->recv_c2s.addrs[0], recv_addr_len);
                atomic_store_explicit(&p->has_client, 1, memory_order_release);
                log_addr_cb("client: ", &p->client_addr);

                if (p->auto_src_port) {
                    int cp = (int)net_addr_port_host(&p->recv_c2s.addrs[0]);
                    if (p->local_port != cp) {
                        p->local_port = cp;
                        char pb2[12];
                        log_info2("src port: auto, reconnecting port=",
                                  u32_to_str(pb2, cp));
                        atomic_store_explicit(&p->reconnect_needed, 1,
                                              memory_order_relaxed);
                    }
                }
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

            uint8_t *data = p->recv_c2s.bufs[i] + prefix;

            if (n >= WG_TRANSPORT_MIN) {
                uint32_t h;
                memcpy(&h, data, 4);
                if (h == WG_TRANSPORT_DATA) {
                    if (!cfg->h4_noop) {
                        uint32_t h4 = pick_h4_cb(p);
                        memcpy(data, &h4, 4);
                    }
                    int total = prefix > 0 ? prefix + n : n;
                    uint8_t *base = prefix > 0 ? p->recv_c2s.bufs[i] : data;
                    p->send_c2s.iovecs[nsend].iov_base = base;
                    p->send_c2s.iovecs[nsend].iov_len = total;
                    nsend++;
                    if (!atomic_exchange_explicit(&p->fe_transport_c2s, 1,
                                                  memory_order_relaxed))
                        log_info("c2s: first transport packet to remote");
                    continue;
                }
            }

            if (nsend > 0) {
                proxy_io_send_batch_gso(p, remote_fd, p->send_c2s.msgs,
                                        p->send_c2s.iovecs, nsend, NULL, 0);
                nsend = 0;
            }

            if (n >= 4) {
                uint32_t hin;
                memcpy(&hin, data, 4);
                if (hin == WG_HANDSHAKE_INIT &&
                    !atomic_exchange_explicit(&p->fe_init_seen, 1,
                                              memory_order_relaxed)) {
                    char nb[12];
                    const char *parts[] = {
                        "c2s: WG handshake init received from client (size=",
                        u32_to_str(nb, n), ")"};
                    log_infon(parts, 3);
                }
            }

            int out_len, sendJunk;
            uint8_t *out =
                transform_outbound(p->recv_c2s.bufs[i], prefix, n, cfg,
                                   fastrand_u64(&p->rng), &out_len, &sendJunk);

            if (sendJunk) {
                log_debug("c2s: handshake init, sending junk");
                proxy_emit_send_junk_and_cps(p, remote_fd);
                int wrapped_len = 0;
                if (out_len > max_obfs_in)
                    continue;
                uint8_t *wrapped =
                    obfs_wrap(&p->obfs_c2s, out, out_len, &wrapped_len);
                if (wrapped) {
                    log_marker_sent_once(&p->obfs_c2s, "c2s");
                    proxy_emit_send_packet(remote_fd, wrapped, wrapped_len);
                }
                if (!atomic_exchange_explicit(&p->fe_init_sent, 1,
                                              memory_order_relaxed)) {
                    char nb[12];
                    const char *parts[] = {
                        "c2s: AWG handshake init forwarded to remote (size=",
                        u32_to_str(nb, out_len), ")"};
                    log_infon(parts, 3);
                }
                continue;
            }

            int wrapped_len = 0;
            if (out_len > max_obfs_in)
                continue;
            uint8_t *wrapped =
                obfs_wrap(&p->obfs_c2s, out, out_len, &wrapped_len);
            if (!wrapped)
                continue;
            log_marker_sent_once(&p->obfs_c2s, "c2s");
            p->send_c2s.iovecs[nsend].iov_base = wrapped;
            p->send_c2s.iovecs[nsend].iov_len = wrapped_len;
            nsend++;
        }

        if (nsend > 0) {
            proxy_io_send_batch_gso(p, remote_fd, p->send_c2s.msgs,
                                    p->send_c2s.iovecs, nsend, NULL, 0);
        }
    }

    return NULL;
}
