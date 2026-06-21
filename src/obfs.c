#include "obfs.h"
#include <string.h>

enum { OBFS_MAX_PACKET = 4096 };
static __thread uint8_t obfs_buf[OBFS_MAX_PACKET];
static const uint8_t k_obfs_marker[4] = {0xA7, 0x51, 0xC3, 0x2E};

static inline void be16_write(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

static inline uint16_t be16_read(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static int streqi(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

awg_obfs_profile_t parse_obfs_profile(const char *s) {
    if (!s || !s[0])
        return AWG_OBFS_OFF;
    if (streqi(s, "off"))
        return AWG_OBFS_OFF;
    if (streqi(s, "stun_ice"))
        return AWG_OBFS_STUN_ICE;
    if (streqi(s, "dtls_record"))
        return AWG_OBFS_DTLS_RECORD;
    if (streqi(s, "rtp_media"))
        return AWG_OBFS_RTP_MEDIA;
    if (streqi(s, "source_query"))
        return AWG_OBFS_SOURCE_QUERY;
    if (streqi(s, "raknet"))
        return AWG_OBFS_RAKNET;
    if (streqi(s, "quic_short"))
        return AWG_OBFS_QUIC_SHORT;
    if (streqi(s, "game_enet"))
        return AWG_OBFS_GAME_ENET;
    if (streqi(s, "game_kcp"))
        return AWG_OBFS_GAME_KCP;
    if (streqi(s, "dns_like"))
        return AWG_OBFS_DNS_LIKE;
    return AWG_OBFS_OFF;
}

int parse_obfs_profile_strict(const char *s, awg_obfs_profile_t *out) {
    if (!s || !s[0] || !out)
        return -1;
    awg_obfs_profile_t profile = parse_obfs_profile(s);
    if (profile == AWG_OBFS_OFF && !streqi(s, "off"))
        return -1;
    *out = profile;
    return 0;
}

const char *obfs_profile_name(awg_obfs_profile_t profile) {
    switch (profile) {
    case AWG_OBFS_STUN_ICE:
        return "stun_ice";
    case AWG_OBFS_DTLS_RECORD:
        return "dtls_record";
    case AWG_OBFS_RTP_MEDIA:
        return "rtp_media";
    case AWG_OBFS_SOURCE_QUERY:
        return "source_query";
    case AWG_OBFS_RAKNET:
        return "raknet";
    case AWG_OBFS_QUIC_SHORT:
        return "quic_short";
    case AWG_OBFS_GAME_ENET:
        return "game_enet";
    case AWG_OBFS_GAME_KCP:
        return "game_kcp";
    case AWG_OBFS_DNS_LIKE:
        return "dns_like";
    default:
        return "off";
    }
}

void obfs_session_init(obfs_session_t *s, awg_obfs_profile_t profile,
                       uint64_t seed) {
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    s->profile = profile;
    s->tx_seq = seed;
    s->rx_seq = seed ^ 0x9e3779b97f4a7c15ULL;
    s->ssrc = (uint32_t)(seed ^ (seed >> 32));
    s->ts = (uint32_t)(seed >> 16);
    s->marker_seen = (profile == AWG_OBFS_OFF) ? 1 : 0;
    s->marker_tx_left = (profile == AWG_OBFS_OFF) ? 0 : 3;
    s->marker_rx_window = (profile == AWG_OBFS_OFF) ? 0 : 16;
}

static int marker_extra_len(const obfs_session_t *s) {
    return (s->marker_tx_left > 0) ? 4 : 0;
}

static void marker_write_if_needed(obfs_session_t *s, uint8_t *dst) {
    if (s->marker_tx_left > 0) {
        memcpy(dst, k_obfs_marker, 4);
        s->marker_tx_left--;
    }
}

static uint8_t *marker_unwrap_if_needed(obfs_session_t *s, uint8_t *in,
                                        int *len) {
    if (*len >= 4 && memcmp(in, k_obfs_marker, 4) == 0) {
        s->marker_seen = 1;
        *len -= 4;
        return in + 4;
    }
    if (s->marker_seen)
        return in;
    if (s->marker_rx_window > 0) {
        s->marker_rx_window--;
        return NULL;
    }
    return NULL;
}

uint8_t *obfs_wrap(obfs_session_t *s, uint8_t *in, int in_len, int *out_len) {
    if (!s || !in || in_len < 0 || !out_len)
        return NULL;
    if (s->profile == AWG_OBFS_OFF) {
        *out_len = in_len;
        s->tx_seq++;
        return in;
    }

    if (s->profile == AWG_OBFS_STUN_ICE) {
        int pad = (int)(s->tx_seq & 3u);
        int mx = marker_extra_len(s);
        int body_len = in_len + pad + mx;
        int total = 20 + body_len;
        if (total > OBFS_MAX_PACKET)
            return NULL;

        /* STUN-like: Binding Request, cookie, txid */
        be16_write(obfs_buf + 0, 0x0001);
        be16_write(obfs_buf + 2, (uint16_t)body_len);
        obfs_buf[4] = 0x21;
        obfs_buf[5] = 0x12;
        obfs_buf[6] = 0xA4;
        obfs_buf[7] = 0x42;
        for (int i = 0; i < 12; i++)
            obfs_buf[8 + i] = (uint8_t)((s->tx_seq >> ((i % 8) * 8)) + i * 17);
        marker_write_if_needed(s, obfs_buf + 20);
        memcpy(obfs_buf + 20 + mx, in, (size_t)in_len);
        if (pad > 0)
            memset(obfs_buf + 20 + mx + in_len, 0, (size_t)pad);
        *out_len = total;
        s->tx_seq++;
        return obfs_buf;
    }

    if (s->profile == AWG_OBFS_DTLS_RECORD) {
        int mx = marker_extra_len(s);
        int total = 13 + mx + in_len;
        uint64_t seq = s->tx_seq & 0x0000FFFFFFFFFFFFULL;
        if (total > OBFS_MAX_PACKET || in_len > 0xFFFF)
            return NULL;
        obfs_buf[0] = 23;
        obfs_buf[1] = 0xFE;
        obfs_buf[2] = 0xFD;
        obfs_buf[3] = 0x00;
        obfs_buf[4] = 0x00;
        obfs_buf[5] = (uint8_t)(seq >> 40);
        obfs_buf[6] = (uint8_t)(seq >> 32);
        obfs_buf[7] = (uint8_t)(seq >> 24);
        obfs_buf[8] = (uint8_t)(seq >> 16);
        obfs_buf[9] = (uint8_t)(seq >> 8);
        obfs_buf[10] = (uint8_t)(seq);
        be16_write(obfs_buf + 11, (uint16_t)(mx + in_len));
        marker_write_if_needed(s, obfs_buf + 13);
        memcpy(obfs_buf + 13 + mx, in, (size_t)in_len);
        *out_len = total;
        s->tx_seq++;
        return obfs_buf;
    }

    if (s->profile == AWG_OBFS_RTP_MEDIA) {
        int mx = marker_extra_len(s);
        int total = 12 + mx + in_len;
        uint16_t seq = (uint16_t)(s->tx_seq & 0xFFFFu);
        if (total > OBFS_MAX_PACKET)
            return NULL;
        obfs_buf[0] = 0x80;
        obfs_buf[1] = 96;
        obfs_buf[2] = (uint8_t)(seq >> 8);
        obfs_buf[3] = (uint8_t)seq;
        s->ts += 3000u;
        obfs_buf[4] = (uint8_t)(s->ts >> 24);
        obfs_buf[5] = (uint8_t)(s->ts >> 16);
        obfs_buf[6] = (uint8_t)(s->ts >> 8);
        obfs_buf[7] = (uint8_t)s->ts;
        obfs_buf[8] = (uint8_t)(s->ssrc >> 24);
        obfs_buf[9] = (uint8_t)(s->ssrc >> 16);
        obfs_buf[10] = (uint8_t)(s->ssrc >> 8);
        obfs_buf[11] = (uint8_t)s->ssrc;
        marker_write_if_needed(s, obfs_buf + 12);
        memcpy(obfs_buf + 12 + mx, in, (size_t)in_len);
        *out_len = total;
        s->tx_seq++;
        return obfs_buf;
    }

    if (s->profile == AWG_OBFS_SOURCE_QUERY) {
        int mx = marker_extra_len(s);
        int total = 6 + mx + in_len;
        if (total > OBFS_MAX_PACKET)
            return NULL;
        obfs_buf[0] = 0xFF;
        obfs_buf[1] = 0xFF;
        obfs_buf[2] = 0xFF;
        obfs_buf[3] = 0xFF;
        obfs_buf[4] = (uint8_t)(0x40 + (s->tx_seq & 0x0F));
        obfs_buf[5] = (uint8_t)(s->tx_seq & 0xFF);
        marker_write_if_needed(s, obfs_buf + 6);
        memcpy(obfs_buf + 6 + mx, in, (size_t)in_len);
        *out_len = total;
        s->tx_seq++;
        return obfs_buf;
    }

    if (s->profile == AWG_OBFS_RAKNET) {
        int mx = marker_extra_len(s);
        int total = 1 + 8 + mx + in_len;
        uint64_t nonce = s->tx_seq ^ 0xA5A5A5A55A5A5A5AULL;
        if (total > OBFS_MAX_PACKET)
            return NULL;
        obfs_buf[0] = 0x84;
        for (int i = 0; i < 8; i++)
            obfs_buf[1 + i] = (uint8_t)(nonce >> ((7 - i) * 8));
        marker_write_if_needed(s, obfs_buf + 9);
        memcpy(obfs_buf + 9 + mx, in, (size_t)in_len);
        *out_len = total;
        s->tx_seq++;
        return obfs_buf;
    }

    if (s->profile == AWG_OBFS_QUIC_SHORT) {
        int mx = marker_extra_len(s);
        int dcid_len = 8 + (int)(s->tx_seq % 5u);
        int pn_len = 1 + (int)(s->tx_seq % 4u);
        int pad_len = (int)(s->tx_seq & 7u);
        int hdr_len = 4 + dcid_len + pn_len;
        int total = hdr_len + mx + in_len + pad_len;
        if (total > OBFS_MAX_PACKET)
            return NULL;
        obfs_buf[0] = (uint8_t)(0x40 | ((s->tx_seq >> 3) & 0x3Fu));
        obfs_buf[1] = (uint8_t)dcid_len;
        obfs_buf[2] = (uint8_t)pn_len;
        obfs_buf[3] = (uint8_t)pad_len;
        for (int i = 0; i < dcid_len; i++)
            obfs_buf[4 + i] = (uint8_t)((s->tx_seq >> ((i % 8) * 8)) + i * 29);
        for (int i = 0; i < pn_len; i++)
            obfs_buf[4 + dcid_len + i] =
                (uint8_t)(s->tx_seq >> ((pn_len - i - 1) * 8));
        marker_write_if_needed(s, obfs_buf + hdr_len);
        memcpy(obfs_buf + hdr_len + mx, in, (size_t)in_len);
        if (pad_len > 0) {
            for (int i = 0; i < pad_len; i++)
                obfs_buf[hdr_len + mx + in_len + i] =
                    (uint8_t)(0x90u + ((s->tx_seq + (uint64_t)i) & 0x3Fu));
        }
        *out_len = total;
        s->tx_seq++;
        return obfs_buf;
    }

    if (s->profile == AWG_OBFS_GAME_ENET) {
        int mx = marker_extra_len(s);
        int cmd_count = 1 + (int)(s->tx_seq % 3u);
        int pad_len = (int)(s->tx_seq % 6u);
        int hdr_len = 6 + cmd_count * 4;
        int total = hdr_len + mx + in_len + pad_len;
        uint16_t peer_id = (uint16_t)(0x8000u | (s->tx_seq & 0x0FFFu));
        uint16_t sent_time = (uint16_t)((s->tx_seq * 7u) & 0xFFFFu);
        static const uint8_t cmd_ids[4] = {0x01, 0x06, 0x07, 0x08};
        if (total > OBFS_MAX_PACKET)
            return NULL;
        obfs_buf[0] = (uint8_t)(peer_id >> 8);
        obfs_buf[1] = (uint8_t)peer_id;
        obfs_buf[2] = (uint8_t)(sent_time >> 8);
        obfs_buf[3] = (uint8_t)sent_time;
        obfs_buf[4] = (uint8_t)cmd_count;
        obfs_buf[5] = (uint8_t)pad_len;
        for (int i = 0; i < cmd_count; i++) {
            int off = 6 + i * 4;
            obfs_buf[off + 0] = cmd_ids[(s->tx_seq + (uint64_t)i) & 3u];
            obfs_buf[off + 1] = (uint8_t)((s->tx_seq + (uint64_t)i) & 0x3u);
            obfs_buf[off + 2] = (uint8_t)((s->tx_seq + (uint64_t)i) >> 8);
            obfs_buf[off + 3] = (uint8_t)(s->tx_seq + (uint64_t)i);
        }
        marker_write_if_needed(s, obfs_buf + hdr_len);
        memcpy(obfs_buf + hdr_len + mx, in, (size_t)in_len);
        if (pad_len > 0) {
            for (int i = 0; i < pad_len; i++)
                obfs_buf[hdr_len + mx + in_len + i] =
                    (uint8_t)(0x30u + ((s->tx_seq + (uint64_t)i * 3u) & 0x4Fu));
        }
        *out_len = total;
        s->tx_seq++;
        return obfs_buf;
    }

    if (s->profile == AWG_OBFS_GAME_KCP) {
        int mx = marker_extra_len(s);
        int conv = (int)(0x01020304u ^ (uint32_t)s->tx_seq);
        int cmd = (int)((s->tx_seq % 4u) + 80u);
        int frg = (int)(s->tx_seq % 3u);
        int wnd = 64 + (int)(s->tx_seq & 0x1Fu);
        int ts = (int)((s->tx_seq * 33u) & 0x7FFFFFFFu);
        int sn = (int)(s->tx_seq & 0x7FFFFFFFu);
        int una = sn - (int)(s->tx_seq % 11u);
        int pad_len = (int)(s->tx_seq % 5u);
        int hdr_len = 24;
        int total = hdr_len + mx + in_len + pad_len;
        if (total > OBFS_MAX_PACKET)
            return NULL;
        obfs_buf[0] = (uint8_t)(conv);
        obfs_buf[1] = (uint8_t)(conv >> 8);
        obfs_buf[2] = (uint8_t)(conv >> 16);
        obfs_buf[3] = (uint8_t)(conv >> 24);
        obfs_buf[4] = (uint8_t)cmd;
        obfs_buf[5] = (uint8_t)frg;
        obfs_buf[6] = (uint8_t)(wnd);
        obfs_buf[7] = (uint8_t)(wnd >> 8);
        obfs_buf[8] = (uint8_t)(ts);
        obfs_buf[9] = (uint8_t)(ts >> 8);
        obfs_buf[10] = (uint8_t)(ts >> 16);
        obfs_buf[11] = (uint8_t)(ts >> 24);
        obfs_buf[12] = (uint8_t)(sn);
        obfs_buf[13] = (uint8_t)(sn >> 8);
        obfs_buf[14] = (uint8_t)(sn >> 16);
        obfs_buf[15] = (uint8_t)(sn >> 24);
        obfs_buf[16] = (uint8_t)(una);
        obfs_buf[17] = (uint8_t)(una >> 8);
        obfs_buf[18] = (uint8_t)(una >> 16);
        obfs_buf[19] = (uint8_t)(una >> 24);
        obfs_buf[20] = (uint8_t)(mx + in_len + pad_len);
        obfs_buf[21] = (uint8_t)((mx + in_len + pad_len) >> 8);
        obfs_buf[22] = 0;
        obfs_buf[23] = (uint8_t)pad_len;
        marker_write_if_needed(s, obfs_buf + hdr_len);
        memcpy(obfs_buf + hdr_len + mx, in, (size_t)in_len);
        for (int i = 0; i < pad_len; i++)
            obfs_buf[hdr_len + mx + in_len + i] =
                (uint8_t)(0x55u + ((s->tx_seq + (uint64_t)i) & 0x33u));
        *out_len = total;
        s->tx_seq++;
        return obfs_buf;
    }

    if (s->profile == AWG_OBFS_DNS_LIKE) {
        int mx = marker_extra_len(s);
        int label_len = 6 + (int)(s->tx_seq % 6u);
        int qname_len = 1 + label_len + 1 + 3 + 1;
        int tail_len = (int)(s->tx_seq % 4u);
        int hdr_len = 12 + qname_len + 4;
        int total = hdr_len + mx + in_len + tail_len;
        uint16_t id = (uint16_t)(s->tx_seq & 0xFFFFu);
        if (total > OBFS_MAX_PACKET)
            return NULL;
        be16_write(obfs_buf + 0, id);
        be16_write(obfs_buf + 2, 0x0100);
        be16_write(obfs_buf + 4, 1);
        be16_write(obfs_buf + 6, 0);
        be16_write(obfs_buf + 8, 0);
        be16_write(obfs_buf + 10, 0);
        obfs_buf[12] = (uint8_t)label_len;
        for (int i = 0; i < label_len; i++)
            obfs_buf[13 + i] =
                (uint8_t)('a' + ((s->tx_seq + (uint64_t)i) % 26u));
        obfs_buf[13 + label_len] = 3;
        obfs_buf[14 + label_len] = 'c';
        obfs_buf[15 + label_len] = 'd';
        obfs_buf[16 + label_len] = 'n';
        obfs_buf[17 + label_len] = 0;
        be16_write(obfs_buf + 18 + label_len, 16);
        be16_write(obfs_buf + 20 + label_len, 1);
        marker_write_if_needed(s, obfs_buf + hdr_len);
        memcpy(obfs_buf + hdr_len + mx, in, (size_t)in_len);
        for (int i = 0; i < tail_len; i++)
            obfs_buf[hdr_len + mx + in_len + i] = 0;
        *out_len = total;
        s->tx_seq++;
        return obfs_buf;
    }

    *out_len = in_len;
    s->tx_seq++;
    return in;
}

uint8_t *obfs_wrap_to(obfs_session_t *s, uint8_t *in, int in_len, uint8_t *out,
                      int out_cap, int *out_len) {
    if (!out || !out_len || out_cap < 0)
        return NULL;
    int wrapped_len = 0;
    uint8_t *wrapped = obfs_wrap(s, in, in_len, &wrapped_len);
    if (!wrapped || wrapped_len > out_cap)
        return NULL;
    if (wrapped_len > 0)
        memmove(out, wrapped, (size_t)wrapped_len);
    *out_len = wrapped_len;
    return out;
}

uint8_t *obfs_unwrap(obfs_session_t *s, uint8_t *in, int in_len, int *out_len) {
    if (!s || !in || in_len < 0 || !out_len)
        return NULL;
    if (s->profile == AWG_OBFS_OFF) {
        *out_len = in_len;
        s->rx_seq++;
        return in;
    }

    if (s->profile == AWG_OBFS_STUN_ICE) {
        if (in_len < 20)
            return NULL;
        if (in[4] != 0x21 || in[5] != 0x12 || in[6] != 0xA4 || in[7] != 0x42)
            return NULL;
        int body_len = (int)be16_read(in + 2);
        if (20 + body_len > in_len)
            return NULL;
        *out_len = body_len;
        uint8_t *out = marker_unwrap_if_needed(s, in + 20, out_len);
        if (!out)
            return NULL;
        s->rx_seq++;
        return out;
    }

    if (s->profile == AWG_OBFS_DTLS_RECORD) {
        if (in_len < 13)
            return NULL;
        if (in[1] != 0xFE || in[2] != 0xFD)
            return NULL;
        int body_len = (int)be16_read(in + 11);
        if (13 + body_len > in_len)
            return NULL;
        *out_len = body_len;
        uint8_t *out = marker_unwrap_if_needed(s, in + 13, out_len);
        if (!out)
            return NULL;
        s->rx_seq++;
        return out;
    }

    if (s->profile == AWG_OBFS_RTP_MEDIA) {
        if (in_len < 12)
            return NULL;
        if ((in[0] >> 6) != 2)
            return NULL;
        *out_len = in_len - 12;
        uint8_t *out = marker_unwrap_if_needed(s, in + 12, out_len);
        if (!out)
            return NULL;
        s->rx_seq++;
        return out;
    }

    if (s->profile == AWG_OBFS_SOURCE_QUERY) {
        if (in_len < 6)
            return NULL;
        if (!(in[0] == 0xFF && in[1] == 0xFF && in[2] == 0xFF && in[3] == 0xFF))
            return NULL;
        *out_len = in_len - 6;
        uint8_t *out = marker_unwrap_if_needed(s, in + 6, out_len);
        if (!out)
            return NULL;
        s->rx_seq++;
        return out;
    }

    if (s->profile == AWG_OBFS_RAKNET) {
        if (in_len < 9)
            return NULL;
        if (in[0] != 0x84)
            return NULL;
        *out_len = in_len - 9;
        uint8_t *out = marker_unwrap_if_needed(s, in + 9, out_len);
        if (!out)
            return NULL;
        s->rx_seq++;
        return out;
    }

    if (s->profile == AWG_OBFS_QUIC_SHORT) {
        if (in_len < 13)
            return NULL;
        if ((in[0] & 0x40) == 0)
            return NULL;
        int dcid_len = (int)in[1];
        int pn_len = (int)in[2];
        int pad_len = (int)in[3];
        int hdr_len = 4 + dcid_len + pn_len;
        if (dcid_len < 8 || dcid_len > 12 || pn_len < 1 || pn_len > 4 ||
            pad_len < 0 || pad_len > 7)
            return NULL;
        if (hdr_len + pad_len > in_len)
            return NULL;
        *out_len = in_len - hdr_len - pad_len;
        uint8_t *out = marker_unwrap_if_needed(s, in + hdr_len, out_len);
        if (!out)
            return NULL;
        s->rx_seq++;
        return out;
    }

    if (s->profile == AWG_OBFS_GAME_ENET) {
        if (in_len < 10)
            return NULL;
        int cmd_count = (int)in[4];
        int pad_len = (int)in[5];
        int hdr_len = 6 + cmd_count * 4;
        if (cmd_count < 1 || cmd_count > 3 || pad_len < 0 || pad_len > 5)
            return NULL;
        if (hdr_len + pad_len > in_len)
            return NULL;
        *out_len = in_len - hdr_len - pad_len;
        uint8_t *out = marker_unwrap_if_needed(s, in + hdr_len, out_len);
        if (!out)
            return NULL;
        s->rx_seq++;
        return out;
    }

    if (s->profile == AWG_OBFS_GAME_KCP) {
        if (in_len < 24)
            return NULL;
        int cmd = (int)in[4];
        int pad_len = (int)in[23];
        int seg_len = (int)in[20] | ((int)in[21] << 8);
        if (cmd < 80 || cmd > 83 || pad_len < 0 || pad_len > 4)
            return NULL;
        if (seg_len > in_len - 24)
            return NULL;
        if (seg_len < pad_len)
            return NULL;
        *out_len = seg_len - pad_len;
        uint8_t *out = marker_unwrap_if_needed(s, in + 24, out_len);
        if (!out)
            return NULL;
        s->rx_seq++;
        return out;
    }

    if (s->profile == AWG_OBFS_DNS_LIKE) {
        if (in_len < 22)
            return NULL;
        int qdcount = (int)be16_read(in + 4);
        int label_len;
        int qname_len;
        int hdr_len;
        if (qdcount != 1)
            return NULL;
        label_len = (int)in[12];
        if (label_len < 6 || label_len > 11)
            return NULL;
        qname_len = 1 + label_len + 1 + 3 + 1;
        hdr_len = 12 + qname_len + 4;
        if (in_len < hdr_len)
            return NULL;
        if (in[13 + label_len] != 3 || in[14 + label_len] != 'c' ||
            in[15 + label_len] != 'd' || in[16 + label_len] != 'n' ||
            in[17 + label_len] != 0)
            return NULL;
        *out_len = in_len - hdr_len;
        uint8_t *out = marker_unwrap_if_needed(s, in + hdr_len, out_len);
        if (!out)
            return NULL;
        s->rx_seq++;
        return out;
    }

    *out_len = in_len;
    s->rx_seq++;
    return in;
}

int obfs_profile_overhead_max(awg_obfs_profile_t profile) {
    switch (profile) {
    case AWG_OBFS_STUN_ICE:
        return 28; /* 20 header + marker + up to 3 pad + margin */
    case AWG_OBFS_DTLS_RECORD:
        return 17;
    case AWG_OBFS_RTP_MEDIA:
        return 16;
    case AWG_OBFS_SOURCE_QUERY:
        return 10;
    case AWG_OBFS_RAKNET:
        return 13;
    case AWG_OBFS_QUIC_SHORT:
        return 31; /* 4 + dcid(12) + pn(4) + marker(4) + pad(7) */
    case AWG_OBFS_GAME_ENET:
        return 27; /* 6 + cmds(12) + marker(4) + pad(5) */
    case AWG_OBFS_GAME_KCP:
        return 32; /* 24 + marker(4) + pad(4) */
    case AWG_OBFS_DNS_LIKE:
        return 34; /* 12 + qname(17) + qtail(4) + marker(4) + tail(3) */
    default:
        return 0;
    }
}
