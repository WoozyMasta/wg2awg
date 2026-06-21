#include "config_file.h"
#include "curve25519.h"
#include "base64.h"
#include "cps.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

typedef enum { SEC_NONE, SEC_INTERFACE, SEC_PEER } section_t;

static void trim(char *s) {
    int len, i = 0;
    while (s[i] == ' ' || s[i] == '\t')
        i++;
    if (i)
        memmove(s, s + i, strlen(s + i) + 1);
    len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = '\0';
}

/* Case-insensitive key comparison */
static int keq(const char *a, const char *b) {
    for (;;) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb)
            return 0;
        if (!ca)
            return 1;
        a++;
        b++;
    }
}

static int parse_int(const char *s, int *out) {
    char *end;
    long v;
    if (!s || !s[0])
        return -1;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno || end == s || *end)
        return -1;
    if (v < INT_MIN || v > INT_MAX)
        return -1;
    *out = (int)v;
    return 0;
}

static int parse_uint32(const char *s, uint32_t *out) {
    char *end;
    unsigned long v;
    if (!s || !s[0])
        return -1;
    errno = 0;
    v = strtoul(s, &end, 10);
    if (errno || end == s || *end)
        return -1;
    if (v > 0xFFFFFFFFu)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

static int parse_hrange(const char *s, hrange_t *out) {
    const char *dash = strchr(s, '-');
    if (!dash) {
        uint32_t v;
        if (parse_uint32(s, &v) < 0)
            return -1;
        out->min = v;
        out->max = v;
        return 0;
    }

    char left[32];
    size_t l = (size_t)(dash - s);
    if (l == 0 || l >= sizeof(left))
        return -1;
    memcpy(left, s, l);
    left[l] = '\0';

    if (parse_uint32(left, &out->min) < 0)
        return -1;
    if (parse_uint32(dash + 1, &out->max) < 0)
        return -1;
    if (out->min > out->max)
        return -1;
    return 0;
}

static int decode_key32(const char *val, uint8_t out[32], const char *field) {
    int n = (int)base64_decode(val, strlen(val), out, 32);
    if (n != 32) {
        const char *parts[] = {"config: ", field, ": invalid base64 key"};
        log_msgn("FATAL: ", parts, 3);
        return -1;
    }
    return 0;
}

/* Commit accumulated peer state into out */
static int flush_peer(awg_file_config_t *out, int *first_peer, int has_pub,
                      const uint8_t pub[32], int has_ep, const char *ep,
                      int has_ka, int ka) {
    if (!has_pub && !has_ep && !has_ka)
        return 0;

    if (has_pub) {
        if (*first_peer) {
            memcpy(out->server_pub, pub, 32);
            out->have_server_pub = 1;
        }
        if (out->peer_pub_count < AWG_MAX_SERVER_PEERS) {
            memcpy(out->peer_pubs[out->peer_pub_count++], pub, 32);
        }
    }
    if (has_ep && *first_peer) {
        strncpy(out->endpoint, ep, sizeof(out->endpoint) - 1);
        out->endpoint[sizeof(out->endpoint) - 1] = '\0';
        out->have_endpoint = 1;
    }
    if (has_ka && *first_peer) {
        out->keepalive = ka;
        out->have_keepalive = 1;
    }
    *first_peer = 0;
    return 0;
}

int config_file_parse(const char *path, awg_file_config_t *out) {
    FILE *f = fopen(path, "r");
    if (!f) {
        log_msg2("FATAL: ", "cannot open config file: ", path);
        return -1;
    }

    memset(out, 0, sizeof(*out));

    char line[1024];
    section_t section = SEC_NONE;
    int first_peer = 1;
    int cur_has_pub = 0;
    uint8_t cur_pub[32];
    int cur_has_ep = 0;
    char cur_ep[256];
    int cur_has_ka = 0;
    int cur_ka = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Strip inline comment */
        char *cm = line;
        while (*cm && *cm != '#' && *cm != ';')
            cm++;
        *cm = '\0';

        trim(line);
        if (!line[0])
            continue;

        /* --- Section header --- */
        if (line[0] == '[') {
            if (section == SEC_PEER)
                if (flush_peer(out, &first_peer, cur_has_pub, cur_pub,
                               cur_has_ep, cur_ep, cur_has_ka, cur_ka) < 0) {
                    fclose(f);
                    return -1;
                }
            cur_has_pub = cur_has_ep = cur_has_ka = 0;

            int len = (int)strlen(line);
            if (len > 2 && line[len - 1] == ']')
                line[len - 1] = '\0';
            char *sec = line + 1;
            trim(sec);

            if (keq(sec, "Interface"))
                section = SEC_INTERFACE;
            else if (keq(sec, "Peer"))
                section = SEC_PEER;
            else
                section = SEC_NONE;
            continue;
        }

        /* --- Key = Value --- */
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);
        if (!key[0] || !val[0])
            continue;

        /* --- [Interface] --- */
        if (section == SEC_INTERFACE) {

            if (keq(key, "PrivateKey")) {
                uint8_t sk[32];
                if (decode_key32(val, sk, "PrivateKey") < 0) {
                    fclose(f);
                    return -1;
                }
                curve25519_public_key(out->client_pub, sk);
                out->have_client_pub = 1;
            } else if (keq(key, "ListenPort")) {
                int port;
                if (parse_int(val, &port) < 0 || port < 1 || port > 65535) {
                    log_msg("FATAL: ",
                            "config: ListenPort: invalid port number");
                    fclose(f);
                    return -1;
                }
                snprintf(out->listen, sizeof(out->listen), "0.0.0.0:%d", port);
                out->have_listen = 1;
            } else if (keq(key, "DNS")) {
                strncpy(out->dns, val, sizeof(out->dns) - 1);
                out->dns[sizeof(out->dns) - 1] = '\0';
                out->have_dns = 1;
            } else if (keq(key, "Jc")) {
                if (parse_int(val, &out->jc) < 0) {
                    log_msg("FATAL: ", "config: Jc: invalid value");
                    fclose(f);
                    return -1;
                }
                out->have_jc = 1;
            } else if (keq(key, "Jmin")) {
                if (parse_int(val, &out->jmin) < 0) {
                    log_msg("FATAL: ", "config: Jmin: invalid value");
                    fclose(f);
                    return -1;
                }
                out->have_jmin = 1;
            } else if (keq(key, "Jmax")) {
                if (parse_int(val, &out->jmax) < 0) {
                    log_msg("FATAL: ", "config: Jmax: invalid value");
                    fclose(f);
                    return -1;
                }
                out->have_jmax = 1;
            } else if (keq(key, "S1")) {
                if (parse_int(val, &out->s1) < 0) {
                    log_msg("FATAL: ", "config: S1: invalid value");
                    fclose(f);
                    return -1;
                }
                out->have_s1 = 1;
            } else if (keq(key, "S2")) {
                if (parse_int(val, &out->s2) < 0) {
                    log_msg("FATAL: ", "config: S2: invalid value");
                    fclose(f);
                    return -1;
                }
                out->have_s2 = 1;
            } else if (keq(key, "S3")) {
                if (parse_int(val, &out->s3) < 0) {
                    log_msg("FATAL: ", "config: S3: invalid value");
                    fclose(f);
                    return -1;
                }
                out->have_s3 = 1;
            } else if (keq(key, "S4")) {
                if (parse_int(val, &out->s4) < 0) {
                    log_msg("FATAL: ", "config: S4: invalid value");
                    fclose(f);
                    return -1;
                }
                out->have_s4 = 1;
            } else if (keq(key, "H1")) {
                if (parse_hrange(val, &out->h1) < 0) {
                    log_msg("FATAL: ", "config: H1: invalid value");
                    fclose(f);
                    return -1;
                }
                out->have_h1 = 1;
            } else if (keq(key, "H2")) {
                if (parse_hrange(val, &out->h2) < 0) {
                    log_msg("FATAL: ", "config: H2: invalid value");
                    fclose(f);
                    return -1;
                }
                out->have_h2 = 1;
            } else if (keq(key, "H3")) {
                if (parse_hrange(val, &out->h3) < 0) {
                    log_msg("FATAL: ", "config: H3: invalid value");
                    fclose(f);
                    return -1;
                }
                out->have_h3 = 1;
            } else if (keq(key, "H4")) {
                if (parse_hrange(val, &out->h4) < 0) {
                    log_msg("FATAL: ", "config: H4: invalid value");
                    fclose(f);
                    return -1;
                }
                out->have_h4 = 1;
            } else if (keq(key, "MorphKey")) {
                if (decode_key32(val, out->morph_key, "MorphKey") < 0) {
                    fclose(f);
                    return -1;
                }
                out->have_morph_key = 1;
            } else if (keq(key, "ObfsProfile")) {
                if (parse_obfs_profile_strict(val, &out->obfs_profile) < 0) {
                    log_msg("FATAL: ", "config: ObfsProfile: invalid profile");
                    fclose(f);
                    return -1;
                }
                out->have_obfs_profile = 1;
            } else if (keq(key, "I1") || keq(key, "I2") || keq(key, "I3") ||
                       keq(key, "I4") || keq(key, "I5")) {
                int idx = key[1] - '1';
                if (idx < 0 || idx >= 5 || cps_parse(val, &out->cps[idx]) < 0) {
                    const char *parts[] = {"config: ", key,
                                           ": invalid CPS template"};
                    log_msgn("FATAL: ", parts, 3);
                    fclose(f);
                    return -1;
                }
                out->have_cps[idx] = 1;
            }
            /* Address, MTU, PostUp, PostDown, Table - ignored, not proxy params
             */
        }

        /* --- [Peer] --- */
        else if (section == SEC_PEER) {

            if (keq(key, "PublicKey")) {
                if (decode_key32(val, cur_pub, "[Peer] PublicKey") < 0) {
                    fclose(f);
                    return -1;
                }
                cur_has_pub = 1;
            } else if (keq(key, "Endpoint")) {
                strncpy(cur_ep, val, sizeof(cur_ep) - 1);
                cur_ep[sizeof(cur_ep) - 1] = '\0';
                cur_has_ep = 1;
            } else if (keq(key, "PersistentKeepalive")) {
                int ka;
                if (parse_int(val, &ka) == 0 && ka > 0) {
                    cur_ka = ka;
                    cur_has_ka = 1;
                }
            }
            /* PresharedKey, AllowedIPs - ignored */
        }
    }

    /* Flush the last [Peer] section */
    if (section == SEC_PEER)
        if (flush_peer(out, &first_peer, cur_has_pub, cur_pub, cur_has_ep,
                       cur_ep, cur_has_ka, cur_ka) < 0) {
            fclose(f);
            return -1;
        }

    fclose(f);
    return 0;
}
