#ifndef AWG_MORPH_H
#define AWG_MORPH_H

#include "transform.h"
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>

/* Slot duration matches WireGuard REKEY_AFTER_TIME (120 s).
 * Five slots (current +/- 2) guarantee at least +/-240 s of clock-skew
 * tolerance regardless of receiver phase within its own slot,
 * comfortably covering the +/-180 s WG clock-skew target.
 * Three slots only guarantee +/-120 s in the worst case
 * (receiver phase near a slot boundary). */
#ifndef MORPH_SLOT_SEC
#define MORPH_SLOT_SEC 120
#endif
#define MORPH_KEY_LEN 32

/* Worst-case junk parameters derived from the formula in morph_derive_profile.
 */
#define MORPH_JC_MAX 8
#define MORPH_JMIN_MIN 40
#define MORPH_JMAX_MAX 998 /* jmin_max(199) + 100 + extra_max(699) */

/* Per-slot handshake obfuscation parameters derived from morph_key + slot. */
typedef struct {
    uint64_t slot;
    hrange_t h1, h2, h3;
    int s1, s2, s3;
    int jc, jmin, jmax;
    int init_total;   /* s1 + WG_INIT_SIZE */
    int resp_total;   /* s2 + WG_RESP_SIZE */
    int cookie_total; /* s3 + WG_COOKIE_SIZE */
} morph_profile_t;

/* One atomic snapshot: 5 profiles (current +/- 2 slots) + precomputed
 * awg_config_t copies. */
#define MORPH_NUM_SLOTS 5
typedef struct {
    uint64_t slot;
    morph_profile_t profiles[MORPH_NUM_SLOTS]; /* [0]=N-2 [1]=N-1 [2]=N
                                                   [3]=N+1 [4]=N+2 */
    awg_config_t cfgs[MORPH_NUM_SLOTS]; /* precomputed, updated atomically */
} morph_snapshot_t;

/* Double-buffer state for lock-free reads from hot I/O threads. */
typedef struct {
    morph_snapshot_t snap[2];
    _Atomic int active_idx;
    _Atomic unsigned readers[2];
    pthread_mutex_t update_lock;
} morph_state_t;

/* Return the current time slot number. */
uint64_t morph_current_slot(void);

/* Derive H4/S4 from morph_key into cfg (called once at startup). */
void morph_derive_static(awg_config_t *cfg, const uint8_t key[MORPH_KEY_LEN]);

/* Derive per-slot handshake parameters from key + slot_num. */
void morph_derive_profile(morph_profile_t *out,
                          const uint8_t key[MORPH_KEY_LEN], uint64_t slot_num);

/* Initialize morph state (prev/curr/next) for the current slot. */
void morph_state_init(morph_state_t *ms, const awg_config_t *base_cfg,
                      const uint8_t key[MORPH_KEY_LEN]);

/* Deterministic variants used by tests and diagnostics. */
void morph_state_init_slot(morph_state_t *ms, const awg_config_t *base_cfg,
                           const uint8_t key[MORPH_KEY_LEN], uint64_t slot);
int morph_update_slot(morph_state_t *ms, const awg_config_t *base_cfg,
                      const uint8_t key[MORPH_KEY_LEN], uint64_t slot);

/* Check if the slot has advanced; if so, publish a new snapshot. */
void morph_update_if_needed(morph_state_t *ms, const awg_config_t *base_cfg,
                            const uint8_t key[MORPH_KEY_LEN]);

/* Pin the active snapshot while a caller uses pointers into it. */
int morph_snapshot_acquire(morph_state_t *ms);
void morph_snapshot_release(morph_state_t *ms, int idx);

/* Return non-zero if len can match a handshake profile in the snapshot. */
int morph_handshake_length_candidate(const morph_snapshot_t *snapshot, int len);

/* Try to decode an inbound AWG handshake packet against prev/curr/next
 * profiles. Returns decoded WG packet pointer (into buf) on success, NULL on
 * failure. Length pre-filter prevents CPU-amplification DoS from spoofed UDP.
 */
uint8_t *morph_transform_inbound(morph_state_t *ms, uint8_t *buf, int len,
                                 int *out_len);

/* Write a CSPRNG-generated base64 morph key to stdout and exit. */
void morph_gen_key(void);

/* Print diagnostic info for the given key string (base64 or hex) and optional
 * slot override (-1 = current slot). */
void morph_probe(const char *key_str, int64_t slot_override);

#endif /* AWG_MORPH_H */
