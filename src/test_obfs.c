#include <string.h>
#include <stdint.h>
#include "test.h"
#include "obfs.h"

static void test_parse_profile(void) {
    ASSERT_EQ(parse_obfs_profile(NULL), AWG_OBFS_OFF);
    ASSERT_EQ(parse_obfs_profile(""), AWG_OBFS_OFF);
    ASSERT_EQ(parse_obfs_profile("stun_ice"), AWG_OBFS_STUN_ICE);
    ASSERT_EQ(parse_obfs_profile("SOURCE_QUERY"), AWG_OBFS_SOURCE_QUERY);
    ASSERT_EQ(parse_obfs_profile("quic_short"), AWG_OBFS_QUIC_SHORT);
    ASSERT_EQ(parse_obfs_profile("GAME_ENET"), AWG_OBFS_GAME_ENET);
    ASSERT_EQ(parse_obfs_profile("game_kcp"), AWG_OBFS_GAME_KCP);
    ASSERT_EQ(parse_obfs_profile("DNS_LIKE"), AWG_OBFS_DNS_LIKE);
    ASSERT_EQ(parse_obfs_profile("unknown"), AWG_OBFS_OFF);
}

static void test_parse_profile_strict(void) {
    awg_obfs_profile_t profile = AWG_OBFS_OFF;
    ASSERT_EQ(parse_obfs_profile_strict("off", &profile), 0);
    ASSERT_EQ(profile, AWG_OBFS_OFF);
    ASSERT_EQ(parse_obfs_profile_strict("DTLS_RECORD", &profile), 0);
    ASSERT_EQ(profile, AWG_OBFS_DTLS_RECORD);
    ASSERT_EQ(parse_obfs_profile_strict("typo", &profile), -1);
    ASSERT_EQ(parse_obfs_profile_strict("", &profile), -1);
}

static void test_wrap_unwrap_passthrough(void) {
    obfs_session_t s;
    uint8_t pkt[32];
    int out_len = -1;

    for (int i = 0; i < 32; i++)
        pkt[i] = (uint8_t)i;

    obfs_session_init(&s, AWG_OBFS_OFF, 7);
    uint8_t *out = obfs_wrap(&s, pkt, 32, &out_len);
    ASSERT(out == pkt);
    ASSERT_EQ(out_len, 32);

    out = obfs_unwrap(&s, pkt, 32, &out_len);
    ASSERT(out == pkt);
    ASSERT_EQ(out_len, 32);
    ASSERT_EQ(s.tx_seq, 8u);
    ASSERT_EQ(s.rx_seq, (7u ^ 0x9e3779b97f4a7c15ULL) + 1u);
}

static void test_wrap_to_uses_caller_buffer(void) {
    obfs_session_t tx, rx;
    uint8_t packet[] = {1, 2, 3, 4, 5};
    uint8_t first[64], second[64];
    int first_len = 0, second_len = 0, plain_len = 0;

    obfs_session_init(&tx, AWG_OBFS_DTLS_RECORD, 7);
    obfs_session_init(&rx, AWG_OBFS_DTLS_RECORD, 7);
    ASSERT(obfs_wrap_to(&tx, packet, sizeof(packet), first, sizeof(first),
                        &first_len) == first);
    ASSERT(obfs_wrap_to(&tx, packet, sizeof(packet), second, sizeof(second),
                        &second_len) == second);
    ASSERT_EQ(first_len, second_len);
    ASSERT(memcmp(first, second, (size_t)first_len) != 0);

    uint8_t *plain = obfs_unwrap(&rx, first, first_len, &plain_len);
    ASSERT(plain != NULL);
    ASSERT_EQ(plain_len, (int)sizeof(packet));
    ASSERT_MEM_EQ(plain, packet, sizeof(packet));
    plain = obfs_unwrap(&rx, second, second_len, &plain_len);
    ASSERT(plain != NULL);
    ASSERT_EQ(plain_len, (int)sizeof(packet));
    ASSERT_MEM_EQ(plain, packet, sizeof(packet));
}

static void test_stun_roundtrip(void) {
    obfs_session_t tx, rx;
    uint8_t pkt[50];
    int out_len = -1;
    int un_len = -1;

    for (int i = 0; i < 50; i++)
        pkt[i] = (uint8_t)(0xA0 + i);

    obfs_session_init(&tx, AWG_OBFS_STUN_ICE, 1);
    obfs_session_init(&rx, AWG_OBFS_STUN_ICE, 2);
    uint8_t *wrapped = obfs_wrap(&tx, pkt, 50, &out_len);
    ASSERT(wrapped != NULL);
    ASSERT(out_len >= 70);
    ASSERT(wrapped[4] == 0x21 && wrapped[5] == 0x12 && wrapped[6] == 0xA4 &&
           wrapped[7] == 0x42);
    uint8_t *plain = obfs_unwrap(&rx, wrapped, out_len, &un_len);
    ASSERT(plain != NULL);
    ASSERT_EQ(un_len, 50);
    ASSERT_MEM_EQ(plain, pkt, 50);
}

static void test_dtls_roundtrip(void) {
    obfs_session_t tx, rx;
    uint8_t pkt[33];
    int out_len = -1;
    int un_len = -1;

    for (int i = 0; i < 33; i++)
        pkt[i] = (uint8_t)i;

    obfs_session_init(&tx, AWG_OBFS_DTLS_RECORD, 5);
    obfs_session_init(&rx, AWG_OBFS_DTLS_RECORD, 6);
    uint8_t *wrapped = obfs_wrap(&tx, pkt, 33, &out_len);
    ASSERT(wrapped != NULL);
    ASSERT_EQ(wrapped[0], 23);
    ASSERT_EQ(wrapped[1], 0xFE);
    ASSERT_EQ(wrapped[2], 0xFD);
    ASSERT_EQ(out_len, 50);
    uint8_t *plain = obfs_unwrap(&rx, wrapped, out_len, &un_len);
    ASSERT(plain != NULL);
    ASSERT_EQ(un_len, 33);
    ASSERT_MEM_EQ(plain, pkt, 33);
}

static void test_unwrap_rejects_invalid(void) {
    obfs_session_t s;
    uint8_t bad1[12] = {0};
    uint8_t bad2[20] = {0};
    int out_len = -1;

    obfs_session_init(&s, AWG_OBFS_STUN_ICE, 1);
    ASSERT(obfs_unwrap(&s, bad1, 12, &out_len) == NULL);

    obfs_session_init(&s, AWG_OBFS_DTLS_RECORD, 1);
    ASSERT(obfs_unwrap(&s, bad2, 20, &out_len) == NULL);
}

static void test_rtp_roundtrip(void) {
    obfs_session_t tx, rx;
    uint8_t pkt[21];
    int out_len = -1;
    int un_len = -1;
    for (int i = 0; i < 21; i++)
        pkt[i] = (uint8_t)(i + 3);
    obfs_session_init(&tx, AWG_OBFS_RTP_MEDIA, 10);
    obfs_session_init(&rx, AWG_OBFS_RTP_MEDIA, 11);
    uint8_t *wrapped = obfs_wrap(&tx, pkt, 21, &out_len);
    ASSERT(wrapped != NULL);
    ASSERT_EQ(out_len, 37);
    ASSERT_EQ((wrapped[0] >> 6), 2);
    uint8_t *plain = obfs_unwrap(&rx, wrapped, out_len, &un_len);
    ASSERT(plain != NULL);
    ASSERT_EQ(un_len, 21);
    ASSERT_MEM_EQ(plain, pkt, 21);
}

static void test_source_query_roundtrip(void) {
    obfs_session_t tx, rx;
    uint8_t pkt[17];
    int out_len = -1;
    int un_len = -1;
    for (int i = 0; i < 17; i++)
        pkt[i] = (uint8_t)(0x70 + i);
    obfs_session_init(&tx, AWG_OBFS_SOURCE_QUERY, 20);
    obfs_session_init(&rx, AWG_OBFS_SOURCE_QUERY, 21);
    uint8_t *wrapped = obfs_wrap(&tx, pkt, 17, &out_len);
    ASSERT(wrapped != NULL);
    ASSERT_EQ(out_len, 27);
    ASSERT(wrapped[0] == 0xFF && wrapped[1] == 0xFF && wrapped[2] == 0xFF &&
           wrapped[3] == 0xFF);
    uint8_t *plain = obfs_unwrap(&rx, wrapped, out_len, &un_len);
    ASSERT(plain != NULL);
    ASSERT_EQ(un_len, 17);
    ASSERT_MEM_EQ(plain, pkt, 17);
}

static void test_raknet_roundtrip(void) {
    obfs_session_t tx, rx;
    uint8_t pkt[19];
    int out_len = -1;
    int un_len = -1;
    for (int i = 0; i < 19; i++)
        pkt[i] = (uint8_t)(0x20 + i);
    obfs_session_init(&tx, AWG_OBFS_RAKNET, 30);
    obfs_session_init(&rx, AWG_OBFS_RAKNET, 31);
    uint8_t *wrapped = obfs_wrap(&tx, pkt, 19, &out_len);
    ASSERT(wrapped != NULL);
    ASSERT_EQ(out_len, 32);
    ASSERT_EQ(wrapped[0], 0x84);
    uint8_t *plain = obfs_unwrap(&rx, wrapped, out_len, &un_len);
    ASSERT(plain != NULL);
    ASSERT_EQ(un_len, 19);
    ASSERT_MEM_EQ(plain, pkt, 19);
}

static void test_overhead_caps(void) {
    ASSERT_EQ(obfs_profile_overhead_max(AWG_OBFS_OFF), 0);
    ASSERT_EQ(obfs_profile_overhead_max(AWG_OBFS_STUN_ICE), 28);
    ASSERT_EQ(obfs_profile_overhead_max(AWG_OBFS_DTLS_RECORD), 17);
    ASSERT_EQ(obfs_profile_overhead_max(AWG_OBFS_RTP_MEDIA), 16);
    ASSERT_EQ(obfs_profile_overhead_max(AWG_OBFS_SOURCE_QUERY), 10);
    ASSERT_EQ(obfs_profile_overhead_max(AWG_OBFS_RAKNET), 13);
    ASSERT_EQ(obfs_profile_overhead_max(AWG_OBFS_QUIC_SHORT), 31);
    ASSERT_EQ(obfs_profile_overhead_max(AWG_OBFS_GAME_ENET), 27);
    ASSERT_EQ(obfs_profile_overhead_max(AWG_OBFS_GAME_KCP), 32);
    ASSERT_EQ(obfs_profile_overhead_max(AWG_OBFS_DNS_LIKE), 34);
}

static void test_quic_short_roundtrip(void) {
    obfs_session_t tx, rx;
    uint8_t pkt[41];
    int out_len = -1;
    int un_len = -1;
    for (int i = 0; i < 41; i++)
        pkt[i] = (uint8_t)(0x40 + i);
    obfs_session_init(&tx, AWG_OBFS_QUIC_SHORT, 40);
    obfs_session_init(&rx, AWG_OBFS_QUIC_SHORT, 41);
    uint8_t *wrapped = obfs_wrap(&tx, pkt, 41, &out_len);
    ASSERT(wrapped != NULL);
    ASSERT(out_len > 41);
    ASSERT((wrapped[0] & 0x40) == 0x40);
    uint8_t *plain = obfs_unwrap(&rx, wrapped, out_len, &un_len);
    ASSERT(plain != NULL);
    ASSERT_EQ(un_len, 41);
    ASSERT_MEM_EQ(plain, pkt, 41);
}

static void test_game_enet_roundtrip(void) {
    obfs_session_t tx, rx;
    uint8_t pkt[23];
    int out_len = -1;
    int un_len = -1;
    for (int i = 0; i < 23; i++)
        pkt[i] = (uint8_t)(0x10 + i);
    obfs_session_init(&tx, AWG_OBFS_GAME_ENET, 55);
    obfs_session_init(&rx, AWG_OBFS_GAME_ENET, 56);
    uint8_t *wrapped = obfs_wrap(&tx, pkt, 23, &out_len);
    ASSERT(wrapped != NULL);
    ASSERT(out_len > 23);
    ASSERT(wrapped[4] >= 1 && wrapped[4] <= 3);
    uint8_t *plain = obfs_unwrap(&rx, wrapped, out_len, &un_len);
    ASSERT(plain != NULL);
    ASSERT_EQ(un_len, 23);
    ASSERT_MEM_EQ(plain, pkt, 23);
}

static void test_game_kcp_roundtrip(void) {
    obfs_session_t tx, rx;
    uint8_t pkt[37];
    int out_len = -1;
    int un_len = -1;
    for (int i = 0; i < 37; i++)
        pkt[i] = (uint8_t)(0x60 + i);
    obfs_session_init(&tx, AWG_OBFS_GAME_KCP, 70);
    obfs_session_init(&rx, AWG_OBFS_GAME_KCP, 71);
    uint8_t *wrapped = obfs_wrap(&tx, pkt, 37, &out_len);
    ASSERT(wrapped != NULL);
    ASSERT(out_len > 37);
    ASSERT(wrapped[4] >= 80 && wrapped[4] <= 83);
    uint8_t *plain = obfs_unwrap(&rx, wrapped, out_len, &un_len);
    ASSERT(plain != NULL);
    ASSERT_EQ(un_len, 37);
    ASSERT_MEM_EQ(plain, pkt, 37);
}

static void test_dns_like_roundtrip(void) {
    obfs_session_t tx, rx;
    uint8_t pkt[29];
    int out_len = -1;
    int un_len = -1;
    for (int i = 0; i < 29; i++)
        pkt[i] = (uint8_t)(0x22 + i);
    obfs_session_init(&tx, AWG_OBFS_DNS_LIKE, 80);
    obfs_session_init(&rx, AWG_OBFS_DNS_LIKE, 81);
    uint8_t *wrapped = obfs_wrap(&tx, pkt, 29, &out_len);
    ASSERT(wrapped != NULL);
    ASSERT(out_len > 29);
    ASSERT_EQ(wrapped[2], 0x01);
    ASSERT_EQ(wrapped[5], 0x01);
    uint8_t *plain = obfs_unwrap(&rx, wrapped, out_len, &un_len);
    ASSERT(plain != NULL);
    ASSERT_EQ(un_len, 29);
    ASSERT_MEM_EQ(plain, pkt, 29);
}

static void test_marker_window_rejects_plain_start(void) {
    obfs_session_t rx;
    uint8_t plain[32];
    int out_len = -1;
    memset(plain, 0, sizeof(plain));
    plain[0] = 23;
    plain[1] = 0xFE;
    plain[2] = 0xFD;
    plain[11] = 0;
    plain[12] = 19;
    for (int i = 0; i < 17; i++)
        plain[13 + i] = (uint8_t)i;
    obfs_session_init(&rx, AWG_OBFS_DTLS_RECORD, 1);
    ASSERT(obfs_unwrap(&rx, plain, 32, &out_len) == NULL);
}

int main(void) {
    RUN_TEST(parse_profile);
    RUN_TEST(parse_profile_strict);
    RUN_TEST(wrap_unwrap_passthrough);
    RUN_TEST(wrap_to_uses_caller_buffer);
    RUN_TEST(stun_roundtrip);
    RUN_TEST(dtls_roundtrip);
    RUN_TEST(rtp_roundtrip);
    RUN_TEST(source_query_roundtrip);
    RUN_TEST(raknet_roundtrip);
    RUN_TEST(quic_short_roundtrip);
    RUN_TEST(game_enet_roundtrip);
    RUN_TEST(game_kcp_roundtrip);
    RUN_TEST(dns_like_roundtrip);
    RUN_TEST(overhead_caps);
    RUN_TEST(marker_window_rejects_plain_start);
    RUN_TEST(unwrap_rejects_invalid);
    TEST_MAIN_END();
}
