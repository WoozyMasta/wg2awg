#include "proxy_s2c_gateway.h"
#include "proxy_emit.h"
#include "morph.h"
#include "log.h"
#include "obfs.h"
#include <string.h>

static obfs_session_t *pick_obfs_tx(proxy_t *p, session_entry_t *dest_entry) {
    if (!dest_entry || p->cfg->mode != AWG_MODE_SERVER)
        return &p->obfs_s2c;
    if (!atomic_load_explicit(&dest_entry->obfs_ready, memory_order_relaxed)) {
        obfs_session_init(&dest_entry->obfs_c2s, p->cfg->obfs_profile,
                          fastrand_u64(&p->rng));
        obfs_session_init(&dest_entry->obfs_s2c, p->cfg->obfs_profile,
                          fastrand_u64(&p->rng));
        atomic_store_explicit(&dest_entry->obfs_ready, 1, memory_order_relaxed);
    }
    return &dest_entry->obfs_s2c;
}

static int obfs_max_packet_len(proxy_t *p) {
    return BUF_SIZE + AWG_PACKET_HEADROOM -
           obfs_profile_overhead_max(p->cfg->obfs_profile);
}

static uint8_t *wrap_for_batch(obfs_session_t *s, uint8_t *in, int in_len,
                               uint8_t *stable_buf, int stable_cap,
                               int *out_len) {
    if (s->profile == AWG_OBFS_OFF)
        return obfs_wrap(s, in, in_len, out_len);
    return obfs_wrap_to(s, in, in_len, stable_buf, stable_cap, out_len);
}

static void log_marker_sent_once(obfs_session_t *s, const char *side) {
    static _Thread_local int logged = 0;
    if (!logged && s->profile != AWG_OBFS_OFF && s->marker_tx_left < 3) {
        const char *parts[] = {side, ": obfs marker sent"};
        log_infon(parts, 2);
        logged = 1;
    }
}

int proxy_s2c_process_gateway(proxy_t *p, uint8_t *base, uint8_t *pkt, int n,
                              int prefix, struct iovec *send_iovecs,
                              struct sockaddr_storage *send_addrs,
                              socklen_t *send_addrlens, int *nsend,
                              uint32_t (*pick_h4_cb)(proxy_t *)) {
    awg_config_t *cfg = p->cfg;
    int server_mode = (cfg->mode == AWG_MODE_SERVER);

    struct sockaddr_storage *dest_addr = NULL;
    struct sockaddr_storage dest_addr_storage;
    socklen_t dest_addr_len = 0;
    session_entry_t *dest_entry = NULL;
    uint32_t msg_type = 0;
    uint32_t expected_sender_index = 0;
    int have_expected_sender_index = 0;
    const uint8_t *out_mac1key = NULL;

    if (server_mode) {
        if (n < 4)
            return 0;
        memcpy(&msg_type, pkt, 4);

        if (msg_type == WG_TRANSPORT_DATA && n >= WG_TRANSPORT_MIN) {
            uint32_t recv_idx;
            memcpy(&recv_idx, pkt + 4, 4);
            if (!session_lookup(p->sessions, recv_idx, &dest_entry,
                                &dest_addr_storage, &dest_addr_len))
                dest_entry = NULL;
            else {
                expected_sender_index = recv_idx;
                have_expected_sender_index = 1;
            }
        } else if (msg_type == WG_HANDSHAKE_RESPONSE && n == WG_RESP_SIZE) {
            uint32_t recv_idx;
            memcpy(&recv_idx, pkt + 8, 4);
            if (!session_lookup(p->sessions, recv_idx, &dest_entry,
                                &dest_addr_storage, &dest_addr_len))
                dest_entry = NULL;
            else {
                expected_sender_index = recv_idx;
                have_expected_sender_index = 1;
            }
        } else if (msg_type == WG_COOKIE_REPLY && n == WG_COOKIE_SIZE) {
            uint32_t recv_idx;
            memcpy(&recv_idx, pkt + 4, 4);
            if (!session_lookup(p->sessions, recv_idx, &dest_entry,
                                &dest_addr_storage, &dest_addr_len))
                dest_entry = NULL;
            else {
                expected_sender_index = recv_idx;
                have_expected_sender_index = 1;
            }
        } else if (msg_type == WG_HANDSHAKE_INIT && n == WG_INIT_SIZE) {
            if (!session_find_sole_entry_with_addr(p->sessions, &dest_entry,
                                                   &dest_addr_storage,
                                                   &dest_addr_len))
                dest_entry = NULL;
            if (dest_entry)
                log_debug("s2c: server: init routed to sole client");
        }
        if (!dest_entry) {
            log_debug("s2c: server: no session for packet, dropping");
            return 0;
        }
        dest_addr = &dest_addr_storage;
    } else {
        if (!atomic_load_explicit(&p->has_client, memory_order_acquire))
            return 0;
        dest_addr = &p->client_addr;
        dest_addr_len = p->client_addr_len;
    }

    if (n >= WG_TRANSPORT_MIN) {
        uint32_t h;
        memcpy(&h, pkt, 4);
        if (h == WG_TRANSPORT_DATA) {
            if (!cfg->h4_noop) {
                uint32_t h4 = pick_h4_cb(p);
                memcpy(pkt, &h4, 4);
            }
            int total = prefix > 0 ? prefix + n : n;
            uint8_t *out = prefix > 0 ? base : pkt;
            if (total > obfs_max_packet_len(p))
                return 0;
            obfs_session_t *obfs_tx = pick_obfs_tx(p, dest_entry);
            int wrapped_len = 0;
            uint8_t *wrapped = wrap_for_batch(
                obfs_tx, out, total, p->send_s2c.bufs[*nsend],
                (int)sizeof(p->send_s2c.bufs[*nsend]), &wrapped_len);
            if (!wrapped)
                return 0;
            log_marker_sent_once(obfs_tx, "s2c");
            int idx = *nsend;
            send_iovecs[idx].iov_base = wrapped;
            send_iovecs[idx].iov_len = wrapped_len;
            send_addrs[idx] = *dest_addr;
            send_addrlens[idx] = dest_addr_len;
            (*nsend)++;
            return 1;
        }
    }

    if (server_mode) {
        int peer_slot = session_get_peer_slot(dest_entry);
        if (peer_slot >= 0 && peer_slot < cfg->server_peer_count)
            out_mac1key = cfg->server_peer_mac1keys[peer_slot];

        if (msg_type == WG_HANDSHAKE_RESPONSE && n == WG_RESP_SIZE &&
            !out_mac1key) {
            int resolved_peer =
                config_server_resolve_peer_for_response(cfg, pkt, n);
            if (resolved_peer >= 0) {
                if (have_expected_sender_index)
                    session_set_peer_slot_if_index(
                        dest_entry, expected_sender_index, resolved_peer);
                else
                    session_set_peer_slot(dest_entry, resolved_peer);
                out_mac1key = cfg->server_peer_mac1keys[resolved_peer];
            }
        }
    }

    /* In morph mode use the current-slot profile cfg for handshakes. */
    const awg_config_t *tx_cfg = cfg;
    int morph_idx = -1;
    if (cfg->morph_enabled) {
        morph_idx = morph_snapshot_acquire(&p->morph);
        tx_cfg = &p->morph.snap[morph_idx].cfgs[1];
    }

    int out_len, sendJunk;
    uint8_t *out = transform_outbound_with_mac1(
        base, prefix, n, tx_cfg, out_mac1key, fastrand_u64(&p->rng), &out_len,
        &sendJunk);

    if (sendJunk) {
        log_debug("s2c: gateway: handshake init, sending junk");
        proxy_emit_send_junk_and_cps_to(p, p->listen_fd, dest_addr,
                                        dest_addr_len);
        if (cfg->morph_enabled)
            proxy_emit_send_junk_cfg_to(p, p->listen_fd, tx_cfg, dest_addr,
                                        dest_addr_len);
        if (morph_idx >= 0)
            morph_snapshot_release(&p->morph, morph_idx);
        obfs_session_t *obfs_tx = pick_obfs_tx(p, dest_entry);
        int wrapped_len = 0;
        if (out_len > obfs_max_packet_len(p))
            return 0;
        uint8_t *wrapped = obfs_wrap(obfs_tx, out, out_len, &wrapped_len);
        if (wrapped) {
            log_marker_sent_once(obfs_tx, "s2c");
            proxy_emit_send_packet_to(p->listen_fd, wrapped, wrapped_len,
                                      dest_addr, dest_addr_len);
        }
        return 1;
    }
    if (morph_idx >= 0)
        morph_snapshot_release(&p->morph, morph_idx);

    obfs_session_t *obfs_tx = pick_obfs_tx(p, dest_entry);
    int wrapped_len = 0;
    if (out_len > obfs_max_packet_len(p))
        return 0;
    uint8_t *wrapped =
        wrap_for_batch(obfs_tx, out, out_len, p->send_s2c.bufs[*nsend],
                       (int)sizeof(p->send_s2c.bufs[*nsend]), &wrapped_len);
    if (!wrapped)
        return 0;
    log_marker_sent_once(obfs_tx, "s2c");
    int idx = *nsend;
    send_iovecs[idx].iov_base = wrapped;
    send_iovecs[idx].iov_len = wrapped_len;
    send_addrs[idx] = *dest_addr;
    send_addrlens[idx] = dest_addr_len;
    (*nsend)++;
    return 1;
}
