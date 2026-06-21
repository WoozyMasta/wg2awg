#include "morph.h"
#include "test.h"
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static const uint8_t test_key[MORPH_KEY_LEN] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static awg_config_t make_base_config(void) {
    awg_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.h1.min = cfg.h1.max = WG_HANDSHAKE_INIT;
    cfg.h2.min = cfg.h2.max = WG_HANDSHAKE_RESPONSE;
    cfg.h3.min = cfg.h3.max = WG_COOKIE_REPLY;
    cfg.h4.min = cfg.h4.max = WG_TRANSPORT_DATA;
    morph_derive_static(&cfg, test_key);
    config_compute(&cfg);
    return cfg;
}

static void test_static_derivation_stable(void) {
    awg_config_t expected = make_base_config();

    for (int i = 0; i < 100; i++) {
        awg_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        morph_derive_static(&cfg, test_key);
        ASSERT_EQ(cfg.h4.min, expected.h4.min);
        ASSERT_EQ(cfg.h4.max, expected.h4.max);
        ASSERT_EQ(cfg.s4, expected.s4);
    }
}

static void test_derivation_vector(void) {
    uint8_t zero_key[MORPH_KEY_LEN] = {0};
    awg_config_t cfg;
    morph_profile_t p;
    memset(&cfg, 0, sizeof(cfg));

    morph_derive_static(&cfg, zero_key);
    morph_derive_profile(&p, zero_key, 12345);

    ASSERT_EQ(cfg.h4.min, 0x60d3251cu);
    ASSERT_EQ(cfg.s4, 234);
    ASSERT_EQ(p.h1.min, 0xea7ad9cfu);
    ASSERT_EQ(p.h2.min, 0xa060da13u);
    ASSERT_EQ(p.h3.min, 0x9c9de5ebu);
    ASSERT_EQ(p.s1, 49);
    ASSERT_EQ(p.s2, 158);
    ASSERT_EQ(p.s3, 301);
    ASSERT_EQ(p.jc, 8);
    ASSERT_EQ(p.jmin, 167);
    ASSERT_EQ(p.jmax, 848);
}

static void test_profile_ranges_and_collisions(void) {
    awg_config_t base = make_base_config();

    for (uint64_t slot = 0; slot < 10000; slot++) {
        morph_profile_t p;
        morph_derive_profile(&p, test_key, slot);

        ASSERT_EQ(p.slot, slot);
        ASSERT(p.h1.min >= 0x00010000u && p.h1.min <= 0xfffeffffu);
        ASSERT(p.h2.min >= 0x00010000u && p.h2.min <= 0xfffeffffu);
        ASSERT(p.h3.min >= 0x00010000u && p.h3.min <= 0xfffeffffu);
        ASSERT(p.h1.min != p.h2.min);
        ASSERT(p.h1.min != p.h3.min);
        ASSERT(p.h2.min != p.h3.min);
        ASSERT(p.h1.min != base.h4.min);
        ASSERT(p.h2.min != base.h4.min);
        ASSERT(p.h3.min != base.h4.min);
        ASSERT(p.s1 >= 0 && p.s1 < 512);
        ASSERT(p.s2 >= 0 && p.s2 < 512);
        ASSERT(p.s3 >= 0 && p.s3 < 512);
        ASSERT(p.jc >= 1 && p.jc <= MORPH_JC_MAX);
        ASSERT(p.jmin >= MORPH_JMIN_MIN && p.jmin <= 199);
        ASSERT(p.jmax > p.jmin && p.jmax <= MORPH_JMAX_MAX);
        ASSERT_EQ(p.init_total, p.s1 + WG_INIT_SIZE);
        ASSERT_EQ(p.resp_total, p.s2 + WG_RESP_SIZE);
        ASSERT_EQ(p.cookie_total, p.s3 + WG_COOKIE_SIZE);
    }
}

static void assert_roundtrip(morph_state_t *ms, const awg_config_t *cfg,
                             uint32_t type, int packet_len) {
    uint8_t buf[AWG_PACKET_BUF_SIZE + AWG_PACKET_HEADROOM];
    const int dataoff = AWG_PACKET_HEADROOM;
    memset(buf, 0xa5, sizeof(buf));
    memcpy(buf + dataoff, &type, sizeof(type));

    int encoded_len = 0;
    int send_junk = 0;
    uint8_t *encoded = transform_outbound(buf, dataoff, packet_len, cfg,
                                          0x12345678, &encoded_len, &send_junk);
    int decoded_len = 0;
    uint8_t *decoded =
        morph_transform_inbound(ms, encoded, encoded_len, &decoded_len);

    ASSERT(decoded != NULL);
    ASSERT_EQ(decoded_len, packet_len);
    uint32_t decoded_type = 0;
    memcpy(&decoded_type, decoded, sizeof(decoded_type));
    ASSERT_EQ(decoded_type, type);
}

static void test_clock_skew_handshake_roundtrip(void) {
    awg_config_t base = make_base_config();
    morph_state_t ms;
    memset(&ms, 0, sizeof(ms));
    morph_state_init(&ms, &base, test_key);

    int idx = morph_snapshot_acquire(&ms);
    awg_config_t cfgs[3];
    memcpy(cfgs, ms.snap[idx].cfgs, sizeof(cfgs));
    morph_snapshot_release(&ms, idx);

    for (int i = 0; i < 3; i++) {
        assert_roundtrip(&ms, &cfgs[i], WG_HANDSHAKE_INIT, WG_INIT_SIZE);
        assert_roundtrip(&ms, &cfgs[i], WG_HANDSHAKE_RESPONSE, WG_RESP_SIZE);
        assert_roundtrip(&ms, &cfgs[i], WG_COOKIE_REPLY, WG_COOKIE_SIZE);
    }
}

static void test_transport_roundtrip(void) {
    awg_config_t base = make_base_config();
    morph_state_t ms;
    memset(&ms, 0, sizeof(ms));
    morph_state_init(&ms, &base, test_key);

    int idx = morph_snapshot_acquire(&ms);
    awg_config_t cfg = ms.snap[idx].cfgs[1];
    morph_snapshot_release(&ms, idx);
    assert_roundtrip(&ms, &cfg, WG_TRANSPORT_DATA, WG_TRANSPORT_MIN + 32);
}

static void test_random_packets_rejected(void) {
    awg_config_t base = make_base_config();
    morph_state_t ms;
    memset(&ms, 0, sizeof(ms));
    morph_state_init(&ms, &base, test_key);

    uint8_t buf[AWG_PACKET_BUF_SIZE];
    for (int len = 4; len < 1000; len++) {
        memset(buf, 0xa5, sizeof(buf));
        int out_len = -1;
        ASSERT(morph_transform_inbound(&ms, buf, len, &out_len) == NULL);
        ASSERT_EQ(out_len, 0);
    }
}

static void test_length_prefilter(void) {
    awg_config_t base = make_base_config();
    morph_state_t ms;
    memset(&ms, 0, sizeof(ms));
    morph_state_init_slot(&ms, &base, test_key, 100);

    int idx = morph_snapshot_acquire(&ms);
    const morph_snapshot_t *snapshot = &ms.snap[idx];
    int rejected = 0;
    for (int len = 4; rejected < 1000; len++) {
        if (morph_handshake_length_candidate(snapshot, len))
            continue;
        ASSERT(!morph_handshake_length_candidate(snapshot, len));
        rejected++;
    }
    for (int i = 0; i < 3; i++) {
        ASSERT(morph_handshake_length_candidate(
            snapshot, snapshot->profiles[i].init_total));
        ASSERT(morph_handshake_length_candidate(
            snapshot, snapshot->profiles[i].resp_total));
        ASSERT(morph_handshake_length_candidate(
            snapshot, snapshot->profiles[i].cookie_total));
    }
    morph_snapshot_release(&ms, idx);
}

typedef struct {
    morph_state_t ms;
    awg_config_t base;
    _Atomic int done;
    _Atomic int failed;
} concurrency_ctx_t;

static void *update_thread(void *arg) {
    concurrency_ctx_t *ctx = arg;
    for (uint64_t slot = 2; slot < 2000; slot++)
        morph_update_slot(&ctx->ms, &ctx->base, test_key, slot);
    atomic_store_explicit(&ctx->done, 1, memory_order_release);
    return NULL;
}

static void *read_thread(void *arg) {
    concurrency_ctx_t *ctx = arg;
    do {
        int idx = morph_snapshot_acquire(&ctx->ms);
        const morph_snapshot_t *s = &ctx->ms.snap[idx];
        int invalid = s->profiles[0].slot != (s->slot > 0 ? s->slot - 1 : 0) ||
                      s->profiles[1].slot != s->slot ||
                      s->profiles[2].slot != s->slot + 1 ||
                      s->cfgs[1].h1.min != s->profiles[1].h1.min ||
                      s->cfgs[1].h2.min != s->profiles[1].h2.min ||
                      s->cfgs[1].h3.min != s->profiles[1].h3.min ||
                      s->cfgs[1].s1 != s->profiles[1].s1 ||
                      s->cfgs[1].s2 != s->profiles[1].s2 ||
                      s->cfgs[1].s3 != s->profiles[1].s3;
        morph_snapshot_release(&ctx->ms, idx);
        if (invalid)
            atomic_store_explicit(&ctx->failed, 1, memory_order_relaxed);
    } while (!atomic_load_explicit(&ctx->done, memory_order_acquire));
    return NULL;
}

static void test_concurrent_snapshot_updates(void) {
    concurrency_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.base = make_base_config();
    atomic_init(&ctx.done, 0);
    atomic_init(&ctx.failed, 0);
    morph_state_init_slot(&ctx.ms, &ctx.base, test_key, 1);

    pthread_t writer;
    pthread_t readers[4];
    ASSERT_EQ(pthread_create(&writer, NULL, update_thread, &ctx), 0);
    for (int i = 0; i < 4; i++)
        ASSERT_EQ(pthread_create(&readers[i], NULL, read_thread, &ctx), 0);

    ASSERT_EQ(pthread_join(writer, NULL), 0);
    for (int i = 0; i < 4; i++)
        ASSERT_EQ(pthread_join(readers[i], NULL), 0);
    ASSERT_EQ(atomic_load_explicit(&ctx.failed, memory_order_relaxed), 0);
}

int main(void) {
    fprintf(stderr, "=== morph tests ===\n");
    RUN_TEST(static_derivation_stable);
    RUN_TEST(derivation_vector);
    RUN_TEST(profile_ranges_and_collisions);
    RUN_TEST(clock_skew_handshake_roundtrip);
    RUN_TEST(transport_roundtrip);
    RUN_TEST(random_packets_rejected);
    RUN_TEST(length_prefilter);
    RUN_TEST(concurrent_snapshot_updates);
    TEST_MAIN_END();
}
