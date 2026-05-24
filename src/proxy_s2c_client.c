#include "proxy_s2c_client.h"
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

int proxy_s2c_process_client(proxy_t *p, uint8_t *pkt, int n,
                             struct iovec *send_iovecs,
                             struct sockaddr_storage *send_addrs,
                             socklen_t *send_addrlens, int *nsend) {
    awg_config_t *cfg = p->cfg;
    int s4 = cfg->s4;
    int unwrapped_len = 0;
    int marker_seen_before = p->obfs_s2c.marker_seen;
    uint8_t *unwrapped = obfs_unwrap(&p->obfs_s2c, pkt, n, &unwrapped_len);
    if (!unwrapped) {
        if (!marker_seen_before)
            log_marker_reject_once(&p->obfs_s2c, "s2c");
        p->obfs_fail_s2c++;
        if (should_log_obfs_fail(p->obfs_fail_s2c)) {
            char cb[12];
            const char *parts[] = {
                "s2c: obfs unwrap failures=", u32_to_str(cb, p->obfs_fail_s2c),
                " profile=", obfs_profile_name(cfg->obfs_profile),
                " (remote likely plain AWG/WG or different obfs preset)"};
            log_msgn("ERROR: ", parts, 6);
        }
        return 0;
    }
    if (!marker_seen_before)
        log_marker_accept_once(&p->obfs_s2c, "s2c");
    pkt = unwrapped;
    n = unwrapped_len;

    if (n >= s4 + WG_TRANSPORT_MIN) {
        if (!cfg->transport_size_ambiguous ||
            (n != cfg->init_total && n != cfg->resp_total &&
             n != cfg->cookie_total)) {
            uint32_t h;
            memcpy(&h, pkt + s4, 4);
            if (hrange_contains(&cfg->h4, h)) {
                uint32_t wt = WG_TRANSPORT_DATA;
                memcpy(pkt + s4, &wt, 4);
                int idx = *nsend;
                send_iovecs[idx].iov_base = pkt + s4;
                send_iovecs[idx].iov_len = n - s4;
                send_addrs[idx] = p->client_addr;
                send_addrlens[idx] = p->client_addr_len;
                (*nsend)++;
                if (!atomic_exchange_explicit(&p->fe_transport_s2c, 1,
                                              memory_order_relaxed))
                    log_info("s2c: first transport packet to client");
                return 1;
            }
        }
    }

    int out_len;
    uint8_t *out = transform_inbound(pkt, n, cfg, &out_len);
    if (!out) {
        log_debug("s2c: junk packet dropped");
        return 0;
    }

    uint32_t mtype = 0;
    if (out_len >= 4)
        memcpy(&mtype, out, 4);
    if (g_log_level >= LOG_DEBUG) {
        const char *type_str = "unknown";
        if (mtype == WG_HANDSHAKE_INIT)
            type_str = "init";
        else if (mtype == WG_HANDSHAKE_RESPONSE)
            type_str = "resp";
        else if (mtype == WG_COOKIE_REPLY)
            type_str = "cookie";
        char nb[12];
        const char *parts[] = {"s2c: handshake type=", type_str,
                               " size=", u32_to_str(nb, out_len)};
        log_debugn(parts, 4);
    }
    if (mtype == WG_HANDSHAKE_RESPONSE &&
        !atomic_exchange_explicit(&p->fe_resp_received, 1,
                                  memory_order_relaxed)) {
        char nb[12];
        const char *parts[] = {"s2c: AWG handshake response received from "
                               "remote, transformed to WG (size=",
                               u32_to_str(nb, out_len), ")"};
        log_infon(parts, 3);
    }

    int idx = *nsend;
    send_iovecs[idx].iov_base = out;
    send_iovecs[idx].iov_len = out_len;
    send_addrs[idx] = p->client_addr;
    send_addrlens[idx] = p->client_addr_len;
    (*nsend)++;
    if (mtype == WG_HANDSHAKE_RESPONSE &&
        !atomic_exchange_explicit(&p->fe_resp_sent, 1, memory_order_relaxed))
        log_info("s2c: WG handshake response delivered to client");
    return 1;
}
