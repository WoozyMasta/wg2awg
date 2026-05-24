#ifndef AWG_OBFS_H
#define AWG_OBFS_H

#include <stdint.h>

typedef enum {
    AWG_OBFS_OFF = 0,
    AWG_OBFS_STUN_ICE,
    AWG_OBFS_DTLS_RECORD,
    AWG_OBFS_RTP_MEDIA,
    AWG_OBFS_SOURCE_QUERY,
    AWG_OBFS_RAKNET,
    AWG_OBFS_QUIC_SHORT,
    AWG_OBFS_GAME_ENET,
    AWG_OBFS_GAME_KCP,
    AWG_OBFS_DNS_LIKE,
} awg_obfs_profile_t;

typedef struct {
    awg_obfs_profile_t profile;
    uint64_t tx_seq;
    uint64_t rx_seq;
    uint32_t ssrc;
    uint32_t ts;
    uint8_t marker_seen;
    uint8_t marker_tx_left;
    uint8_t marker_rx_window;
} obfs_session_t;

awg_obfs_profile_t parse_obfs_profile(const char *s);
const char *obfs_profile_name(awg_obfs_profile_t profile);

void obfs_session_init(obfs_session_t *s, awg_obfs_profile_t profile,
                       uint64_t seed);

uint8_t *obfs_wrap(obfs_session_t *s, uint8_t *in, int in_len, int *out_len);
uint8_t *obfs_unwrap(obfs_session_t *s, uint8_t *in, int in_len, int *out_len);
int obfs_profile_overhead_max(awg_obfs_profile_t profile);

#endif
