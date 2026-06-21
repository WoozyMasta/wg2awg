#include "morph.h"
#include "blake2s.h"
#include "base64.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sched.h>
#include <limits.h>

#define MORPH_WRITER_LOCK UINT_MAX

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* Map an arbitrary uint32 into the valid H range [0x00010000, 0xFFFEFFFF].
 * This range avoids WireGuard reserved type bytes (0-4 in the low byte). */
static uint32_t h_map_range(uint32_t x) {
    const uint32_t lo = 0x00010000u;
    const uint64_t span = (uint64_t)0xFFFEFFFFu - lo + 1u;
    return lo + (uint32_t)((uint64_t)x % span);
}

/* Derive an H value that doesn't collide with any previously derived values.
 * Uses a deterministic counter-based loop to resolve rare collisions. */
static uint32_t derive_h(const uint8_t *slot_key, int off, const uint32_t *prev,
                         int nprev) {
    uint32_t base = read_u32_le(slot_key + off);
    for (uint32_t c = 0;; c++) {
        uint32_t tmp = base ^ (c * 0x9E3779B9u);
        uint32_t v = h_map_range(tmp);
        if (v < 5)
            continue; /* extra guard: skip WG reserved type values */
        int dup = 0;
        for (int i = 0; i < nprev; i++)
            if (prev[i] == v) {
                dup = 1;
                break;
            }
        if (!dup)
            return v;
    }
}

/* KDF: "wg2awg morph v1" || morph_key || uint64_le(slot) -> 32 bytes */
static void kdf_slot(uint8_t out[32], const uint8_t key[MORPH_KEY_LEN],
                     uint64_t slot) {
    static const char ctx[] = "wg2awg morph v1";
    enum { CTX_LEN = sizeof(ctx) - 1 };
    uint8_t buf[CTX_LEN + MORPH_KEY_LEN + 8];
    memcpy(buf, ctx, CTX_LEN);
    memcpy(buf + CTX_LEN, key, MORPH_KEY_LEN);
    uint8_t *p = buf + CTX_LEN + MORPH_KEY_LEN;
    uint64_t s = slot;
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(s & 0xFF);
        s >>= 8;
    }
    blake2s_256(buf, sizeof(buf), out);
}

/* KDF: "wg2awg morph static v1" || morph_key -> 32 bytes */
static void kdf_static(uint8_t out[32], const uint8_t key[MORPH_KEY_LEN]) {
    static const char ctx[] = "wg2awg morph static v1";
    enum { CTX_LEN = sizeof(ctx) - 1 };
    uint8_t buf[CTX_LEN + MORPH_KEY_LEN];
    memcpy(buf, ctx, CTX_LEN);
    memcpy(buf + CTX_LEN, key, MORPH_KEY_LEN);
    blake2s_256(buf, sizeof(buf), out);
}

/* Build an awg_config_t for a given profile.
 * H4/S4 come from base_cfg (set by morph_derive_static at startup). */
static awg_config_t cfg_from_profile(const awg_config_t *base_cfg,
                                     const morph_profile_t *p) {
    awg_config_t c = *base_cfg;
    c.h1 = p->h1;
    c.h2 = p->h2;
    c.h3 = p->h3;
    c.s1 = p->s1;
    c.s2 = p->s2;
    c.s3 = p->s3;
    c.jc = p->jc;
    c.jmin = p->jmin;
    c.jmax = p->jmax;
    config_compute(&c);
    return c;
}

uint64_t morph_current_slot(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec / MORPH_SLOT_SEC;
}

void morph_derive_static(awg_config_t *cfg, const uint8_t key[MORPH_KEY_LEN]) {
    uint8_t sk[32];
    kdf_static(sk, key);
    uint32_t h4 = h_map_range(read_u32_le(sk));
    if (h4 < 5)
        h4 += 5; /* belt-and-suspenders */
    cfg->h4.min = cfg->h4.max = h4;
    cfg->s4 = (int)(read_u16_le(sk + 4) % 256);
}

void morph_derive_profile(morph_profile_t *out,
                          const uint8_t key[MORPH_KEY_LEN], uint64_t slot_num) {
    uint8_t sk[32];
    uint8_t static_key[32];
    kdf_slot(sk, key, slot_num);
    kdf_static(static_key, key);

    /* H1/H2/H3 must not overlap each other or the static H4. */
    uint32_t hs[4];
    hs[0] = h_map_range(read_u32_le(static_key));
    hs[1] = derive_h(sk, 0, hs, 1);
    hs[2] = derive_h(sk, 4, hs, 2);
    hs[3] = derive_h(sk, 8, hs, 3);
    out->h1 = (hrange_t){hs[1], hs[1]};
    out->h2 = (hrange_t){hs[2], hs[2]};
    out->h3 = (hrange_t){hs[3], hs[3]};

    out->s1 = (int)(read_u16_le(sk + 12) % 512);
    out->s2 = (int)(read_u16_le(sk + 14) % 512);
    out->s3 = (int)(read_u16_le(sk + 16) % 512);
    out->jc = (int)((sk[18] % MORPH_JC_MAX) + 1);
    out->jmin = MORPH_JMIN_MIN + (int)(read_u16_le(sk + 19) % 160);
    out->jmax = out->jmin + 100 + (int)(read_u16_le(sk + 21) % 700);

    out->slot = slot_num;
    out->init_total = out->s1 + WG_INIT_SIZE;
    out->resp_total = out->s2 + WG_RESP_SIZE;
    out->cookie_total = out->s3 + WG_COOKIE_SIZE;
}

void morph_state_init_slot(morph_state_t *ms, const awg_config_t *base_cfg,
                           const uint8_t key[MORPH_KEY_LEN], uint64_t slot) {
    pthread_mutex_init(&ms->update_lock, NULL);
    atomic_init(&ms->readers[0], 0);
    atomic_init(&ms->readers[1], 0);
    atomic_store_explicit(&ms->active_idx, 0, memory_order_relaxed);

    morph_snapshot_t *s = &ms->snap[0];
    s->slot = slot;

    morph_derive_profile(&s->profiles[0], key, slot > 0 ? slot - 1 : 0);
    morph_derive_profile(&s->profiles[1], key, slot);
    morph_derive_profile(&s->profiles[2], key, slot + 1);
    s->cfgs[0] = cfg_from_profile(base_cfg, &s->profiles[0]);
    s->cfgs[1] = cfg_from_profile(base_cfg, &s->profiles[1]);
    s->cfgs[2] = cfg_from_profile(base_cfg, &s->profiles[2]);
}

void morph_state_init(morph_state_t *ms, const awg_config_t *base_cfg,
                      const uint8_t key[MORPH_KEY_LEN]) {
    morph_state_init_slot(ms, base_cfg, key, morph_current_slot());
}

int morph_update_slot(morph_state_t *ms, const awg_config_t *base_cfg,
                      const uint8_t key[MORPH_KEY_LEN], uint64_t now_slot) {
    pthread_mutex_lock(&ms->update_lock);
    int cur = atomic_load_explicit(&ms->active_idx, memory_order_relaxed);
    if (ms->snap[cur].slot == now_slot) {
        pthread_mutex_unlock(&ms->update_lock);
        return 0;
    }

    int nxt = cur ^ 1;
    for (;;) {
        unsigned expected = 0;
        if (atomic_compare_exchange_weak_explicit(
                &ms->readers[nxt], &expected, MORPH_WRITER_LOCK,
                memory_order_acquire, memory_order_relaxed))
            break;
        sched_yield();
    }

    morph_snapshot_t *s = &ms->snap[nxt];
    s->slot = now_slot;

    /* Derive all slots: wall-clock jumps may skip or move slots. */
    morph_derive_profile(&s->profiles[0], key, now_slot > 0 ? now_slot - 1 : 0);
    morph_derive_profile(&s->profiles[1], key, now_slot);
    morph_derive_profile(&s->profiles[2], key, now_slot + 1);
    s->cfgs[0] = cfg_from_profile(base_cfg, &s->profiles[0]);
    s->cfgs[1] = cfg_from_profile(base_cfg, &s->profiles[1]);
    s->cfgs[2] = cfg_from_profile(base_cfg, &s->profiles[2]);

    atomic_store_explicit(&ms->active_idx, nxt, memory_order_release);
    atomic_store_explicit(&ms->readers[nxt], 0, memory_order_release);
    pthread_mutex_unlock(&ms->update_lock);
    return 1;
}

void morph_update_if_needed(morph_state_t *ms, const awg_config_t *base_cfg,
                            const uint8_t key[MORPH_KEY_LEN]) {
    uint64_t now_slot = morph_current_slot();
    if (!morph_update_slot(ms, base_cfg, key, now_slot))
        return;
    {
        char nb[24];
        const char *parts[] = {"morph: slot=",
                               u32_to_str(nb, (uint32_t)now_slot)};
        log_infon(parts, 2);
    }
}

int morph_snapshot_acquire(morph_state_t *ms) {
    for (;;) {
        int idx = atomic_load_explicit(&ms->active_idx, memory_order_acquire);
        unsigned readers =
            atomic_load_explicit(&ms->readers[idx], memory_order_relaxed);
        while (readers != MORPH_WRITER_LOCK) {
            if (atomic_compare_exchange_weak_explicit(
                    &ms->readers[idx], &readers, readers + 1,
                    memory_order_acquire, memory_order_relaxed))
                break;
        }
        if (readers == MORPH_WRITER_LOCK)
            continue;
        if (idx == atomic_load_explicit(&ms->active_idx, memory_order_acquire))
            return idx;
        atomic_fetch_sub_explicit(&ms->readers[idx], 1, memory_order_release);
    }
}

void morph_snapshot_release(morph_state_t *ms, int idx) {
    atomic_fetch_sub_explicit(&ms->readers[idx], 1, memory_order_release);
}

int morph_handshake_length_candidate(const morph_snapshot_t *snapshot,
                                     int len) {
    for (int i = 0; i < 3; i++) {
        const morph_profile_t *p = &snapshot->profiles[i];
        if (len == p->init_total || len == p->resp_total ||
            len == p->cookie_total)
            return 1;
    }
    return 0;
}

uint8_t *morph_transform_inbound(morph_state_t *ms, uint8_t *buf, int len,
                                 int *out_len) {
    int idx = morph_snapshot_acquire(ms);
    const morph_snapshot_t *s = &ms->snap[idx];
    /* Try curr first (most likely), then prev, then next. */
    static const int order[3] = {1, 0, 2};

    if (morph_handshake_length_candidate(s, len)) {
        for (int i = 0; i < 3; i++) {
            const morph_profile_t *p = &s->profiles[order[i]];
            if (len != p->init_total && len != p->resp_total &&
                len != p->cookie_total)
                continue;
            uint8_t *r =
                transform_inbound(buf, len, &s->cfgs[order[i]], out_len);
            if (r) {
                morph_snapshot_release(ms, idx);
                return r;
            }
        }
    }

    /* H4/S4 are static, so the current config can decode transport packets. */
    uint8_t *r = transform_inbound(buf, len, &s->cfgs[1], out_len);
    morph_snapshot_release(ms, idx);
    if (r)
        return r;

    *out_len = 0;
    return NULL;
}

static int decode_morph_key(uint8_t out[MORPH_KEY_LEN], const char *key_str) {
    size_t len = strlen(key_str);
    if (len == 44) {
        /* Standard base64: 32 bytes → 44 chars (with one '=' padding). */
        if (base64_decode(key_str, len, out, MORPH_KEY_LEN) != MORPH_KEY_LEN)
            return -1;
        return 0;
    }
    if (len == 64) {
        /* Hex: 32 bytes → 64 hex chars. */
        for (int i = 0; i < MORPH_KEY_LEN; i++) {
            char hi = key_str[i * 2];
            char lo = key_str[i * 2 + 1];
            int a = (hi >= '0' && hi <= '9')   ? hi - '0'
                    : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10
                    : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10
                                               : -1;
            int b = (lo >= '0' && lo <= '9')   ? lo - '0'
                    : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10
                    : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10
                                               : -1;
            if (a < 0 || b < 0)
                return -1;
            out[i] = (uint8_t)((a << 4) | b);
        }
        return 0;
    }
    return -1; /* unsupported length */
}

void morph_gen_key(void) {
    uint8_t key[MORPH_KEY_LEN];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        log_error("morph-gen-key: open /dev/urandom failed");
        _exit(1);
    }
    size_t off = 0;
    while (off < MORPH_KEY_LEN) {
        ssize_t n = read(fd, key + off, MORPH_KEY_LEN - off);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        close(fd);
        log_error("morph-gen-key: read /dev/urandom failed");
        _exit(1);
    }
    close(fd);

    char b64[48];
    base64_encode(key, MORPH_KEY_LEN, b64);
    puts(b64);
}

void morph_probe(const char *key_str, int64_t slot_override) {
    uint8_t key[MORPH_KEY_LEN];
    if (decode_morph_key(key, key_str) < 0) {
        fputs("morph-probe: key must be 44-char base64 or 64-char hex\n",
              stderr);
        _exit(1);
    }

    /* Static params */
    awg_config_t tmp_cfg;
    memset(&tmp_cfg, 0, sizeof(tmp_cfg));
    morph_derive_static(&tmp_cfg, key);

    /* Per-slot params */
    uint64_t slot =
        (slot_override >= 0) ? (uint64_t)slot_override : morph_current_slot();
    morph_profile_t prof;
    morph_derive_profile(&prof, key, slot);

    /* Print key prefix (first 12 chars of base64) */
    char b64[48];
    base64_encode(key, MORPH_KEY_LEN, b64);
    b64[12] = '\0';

    printf("Morph probe (key: %s...)\n", b64);
    printf("Static:  H4=0x%08X  S4=%d\n", tmp_cfg.h4.min, tmp_cfg.s4);
    printf("Slot:    %llu  (slot_sec=%d)\n", (unsigned long long)slot,
           MORPH_SLOT_SEC);
    printf("H1: 0x%08X  H2: 0x%08X  H3: 0x%08X\n", prof.h1.min, prof.h2.min,
           prof.h3.min);
    printf("S1: %d  S2: %d  S3: %d\n", prof.s1, prof.s2, prof.s3);
    printf("Jc: %d  Jmin: %d  Jmax: %d\n", prof.jc, prof.jmin, prof.jmax);
    printf("init_total: %d  resp_total: %d  cookie_total: %d\n",
           prof.init_total, prof.resp_total, prof.cookie_total);
}
