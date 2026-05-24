#include "config_runtime.h"
#include "base64.h"
#include "cps.h"
#include "obfs.h"
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

static int parse_uint32_str(const char *s, uint32_t *out) {
    char *end = NULL;
    unsigned long v;
    if (!s || !s[0] || !out)
        return -1;
    errno = 0;
    v = strtoul(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0')
        return -1;
    if (v > UINT32_MAX)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

static int streqi(const char *a, const char *b) {
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

int parse_awg_mode(const char *s, int *mode_out) {
    if (!s || !s[0] || !mode_out)
        return -1;

    if (streqi(s, "client")) {
        *mode_out = AWG_MODE_CLIENT;
        return 0;
    }
    if (streqi(s, "gateway")) {
        *mode_out = AWG_MODE_GATEWAY;
        return 0;
    }
    if (streqi(s, "server")) {
        *mode_out = AWG_MODE_SERVER;
        return 0;
    }
    return -1;
}

int parse_log_level(const char *s, int *level_out) {
    if (!s || !s[0] || !level_out)
        return -1;

    switch (s[0]) {
    case 'n':
    case 'N':
        *level_out = LOG_NONE;
        return 0;
    case 'e':
    case 'E':
        *level_out = LOG_ERROR;
        return 0;
    case 'i':
    case 'I':
        *level_out = LOG_INFO;
        return 0;
    case 'd':
    case 'D':
        *level_out = LOG_DEBUG;
        return 0;
    default:
        return -1;
    }
}

int parse_int_strict(const char *s, int *out) {
    char *end = NULL;
    long v;
    if (!s || !s[0] || !out)
        return -1;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0')
        return -1;
    if (v < INT_MIN || v > INT_MAX)
        return -1;
    *out = (int)v;
    return 0;
}

int parse_src_port(const char *s, int *src_port_out) {
    int v;
    if (!s || !s[0] || !src_port_out)
        return -1;
    if (streqi(s, "auto")) {
        *src_port_out = 0;
        return 0;
    }
    if (parse_int_strict(s, &v) < 0)
        return -1;
    *src_port_out = v;
    return 0;
}

int write_dns_resolv_conf(const char *path, const char *dns_str) {
    FILE *f;
    const char *p;
    if (!path || !path[0] || !dns_str || !dns_str[0])
        return -1;

    f = fopen(path, "w");
    if (!f)
        return -1;

    p = dns_str;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && *p != ',' && *p != ' ' && *p != '\t')
            p++;
        int len = (int)(p - start);
        if (len > 0 && len < 64) {
            char ns[64];
            memcpy(ns, start, (size_t)len);
            ns[len] = '\0';
            fprintf(f, "nameserver %s\n", ns);
        }
    }

    if (fclose(f) != 0)
        return -1;
    return 0;
}

static int is_pub_sep(char c) {
    return c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' ||
           c == '\n';
}

int add_server_peer_pub_unique(awg_config_t *cfg, const uint8_t pub[32]) {
    if (!cfg || !pub)
        return -1;
    for (int i = 0; i < cfg->server_peer_count; i++) {
        if (memcmp(cfg->server_peer_pubs[i], pub, 32) == 0)
            return 0;
    }
    if (cfg->server_peer_count >= AWG_MAX_SERVER_PEERS)
        return -1;
    memcpy(cfg->server_peer_pubs[cfg->server_peer_count++], pub, 32);
    return 0;
}

int parse_server_peer_list(awg_config_t *cfg, const char *value) {
    const char *p;
    if (!cfg || !value)
        return -1;

    p = value;
    while (*p) {
        while (*p && is_pub_sep(*p))
            p++;
        if (!*p)
            break;

        const char *start = p;
        while (*p && !is_pub_sep(*p))
            p++;

        int len = (int)(p - start);
        if (len <= 0)
            continue;
        if (len >= 128)
            return -1;

        char token[128];
        uint8_t pub[32];
        memcpy(token, start, (size_t)len);
        token[len] = '\0';

        if (base64_decode(token, (size_t)len, pub, 32) != 32)
            return -1;
        if (add_server_peer_pub_unique(cfg, pub) < 0)
            return -1;
    }
    return 0;
}

int load_server_peer_file(awg_config_t *cfg, const char *path) {
    enum { MAX_CLIENT_PUBS_FILE_SIZE = 1024 * 1024 };
    FILE *f;
    long size;
    char *buf;
    int rc;
    if (!cfg || !path || !path[0])
        return -1;

    f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    size = ftell(f);
    if (size < 0 || size > MAX_CLIENT_PUBS_FILE_SIZE) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[size] = '\0';
    rc = parse_server_peer_list(cfg, buf);
    free(buf);
    return rc;
}

int parse_hrange_str(const char *s, hrange_t *r) {
    const char *dash = NULL;
    if (!s || !s[0] || !r)
        return -1;
    for (const char *p = s; *p; p++) {
        if (*p == '-') {
            dash = p;
            break;
        }
    }
    if (!dash) {
        uint32_t v;
        if (parse_uint32_str(s, &v) < 0)
            return -1;
        r->min = r->max = v;
        return 0;
    }

    char tmp[32];
    int hlen = (int)(dash - s);
    if (hlen <= 0 || hlen >= (int)sizeof(tmp))
        return -1;
    for (const char *p = dash + 1; *p; p++) {
        if (*p == '-')
            return -1;
    }
    memcpy(tmp, s, (size_t)hlen);
    tmp[hlen] = '\0';

    if (parse_uint32_str(tmp, &r->min) < 0)
        return -1;
    if (parse_uint32_str(dash + 1, &r->max) < 0)
        return -1;
    if (r->min > r->max)
        return -1;
    return 0;
}

int load_operational_env(awg_config_t *cfg, const char **err_msg) {
    const char *v;
    int iv;
    if (!cfg)
        return -1;

    cfg->timeout = 180;
    v = getenv("AWG_TIMEOUT");
    if (v && v[0]) {
        if (parse_int_strict(v, &iv) < 0) {
            if (err_msg)
                *err_msg = "AWG_TIMEOUT: invalid integer";
            return -1;
        }
        cfg->timeout = iv;
    }

    cfg->remote_silent_timeout = 300;
    v = getenv("AWG_REMOTE_SILENT_TIMEOUT");
    if (v && v[0]) {
        if (parse_int_strict(v, &iv) < 0) {
            if (err_msg)
                *err_msg = "AWG_REMOTE_SILENT_TIMEOUT: invalid integer";
            return -1;
        }
        cfg->remote_silent_timeout = iv;
    }

    cfg->connect_retries = 0;
    v = getenv("AWG_CONNECT_RETRIES");
    if (v && v[0]) {
        if (parse_int_strict(v, &iv) < 0) {
            if (err_msg)
                *err_msg = "AWG_CONNECT_RETRIES: invalid integer";
            return -1;
        }
        if (iv < 0) {
            if (err_msg)
                *err_msg = "AWG_CONNECT_RETRIES: must be >= 0";
            return -1;
        }
        cfg->connect_retries = iv;
    }

    cfg->socket_buf = 16 * 1024 * 1024;
    v = getenv("AWG_SOCKET_BUF");
    if (v && v[0]) {
        if (parse_int_strict(v, &iv) < 0) {
            if (err_msg)
                *err_msg = "AWG_SOCKET_BUF: invalid integer";
            return -1;
        }
        cfg->socket_buf = iv;
    }

    return 0;
}

int load_network_perf_env(awg_config_t *cfg, const char **err_msg) {
    const char *v;
    int iv;
    if (!cfg)
        return -1;

    cfg->src_port = 0;
    v = getenv("AWG_SRC_PORT");
    if (v && v[0]) {
        if (parse_src_port(v, &iv) < 0) {
            if (err_msg)
                *err_msg = "AWG_SRC_PORT: invalid integer";
            return -1;
        }
        cfg->src_port = iv;
    }

    cfg->cpu_c2s = -1;
    v = getenv("AWG_CPU_C2S");
    if (v && v[0]) {
        if (parse_int_strict(v, &iv) < 0) {
            if (err_msg)
                *err_msg = "AWG_CPU_C2S: invalid integer";
            return -1;
        }
        cfg->cpu_c2s = iv;
    }

    cfg->cpu_s2c = -1;
    v = getenv("AWG_CPU_S2C");
    if (v && v[0]) {
        if (parse_int_strict(v, &iv) < 0) {
            if (err_msg)
                *err_msg = "AWG_CPU_S2C: invalid integer";
            return -1;
        }
        cfg->cpu_s2c = iv;
    }

    cfg->busy_poll = 0;
    v = getenv("AWG_BUSY_POLL");
    if (v && v[0]) {
        if (parse_int_strict(v, &iv) < 0) {
            if (err_msg)
                *err_msg = "AWG_BUSY_POLL: invalid integer";
            return -1;
        }
        cfg->busy_poll = iv;
    }

    cfg->no_gro = 0;
    v = getenv("AWG_NO_GRO");
    if (v && v[0]) {
        if (parse_int_strict(v, &iv) < 0) {
            if (err_msg)
                *err_msg = "AWG_NO_GRO: invalid integer";
            return -1;
        }
        cfg->no_gro = iv;
    }

    return 0;
}

int load_obfuscation_env(awg_config_t *cfg, const char **err_msg) {
    const char *v;
    if (!cfg)
        return -1;

    if (parse_int_strict(getenv("AWG_JC") ? getenv("AWG_JC") : "0", &cfg->jc) <
        0) {
        if (err_msg)
            *err_msg = "AWG_JC: invalid integer";
        return -1;
    }
    if (parse_int_strict(getenv("AWG_JMIN") ? getenv("AWG_JMIN") : "0",
                         &cfg->jmin) < 0) {
        if (err_msg)
            *err_msg = "AWG_JMIN: invalid integer";
        return -1;
    }
    if (parse_int_strict(getenv("AWG_JMAX") ? getenv("AWG_JMAX") : "0",
                         &cfg->jmax) < 0) {
        if (err_msg)
            *err_msg = "AWG_JMAX: invalid integer";
        return -1;
    }
    if (parse_int_strict(getenv("AWG_S1") ? getenv("AWG_S1") : "0", &cfg->s1) <
        0) {
        if (err_msg)
            *err_msg = "AWG_S1: invalid integer";
        return -1;
    }
    if (parse_int_strict(getenv("AWG_S2") ? getenv("AWG_S2") : "0", &cfg->s2) <
        0) {
        if (err_msg)
            *err_msg = "AWG_S2: invalid integer";
        return -1;
    }

    if (parse_hrange_str(getenv("AWG_H1") ? getenv("AWG_H1") : "1", &cfg->h1) <
        0) {
        if (err_msg)
            *err_msg = "AWG_H1: invalid range";
        return -1;
    }
    if (parse_hrange_str(getenv("AWG_H2") ? getenv("AWG_H2") : "2", &cfg->h2) <
        0) {
        if (err_msg)
            *err_msg = "AWG_H2: invalid range";
        return -1;
    }
    if (parse_hrange_str(getenv("AWG_H3") ? getenv("AWG_H3") : "3", &cfg->h3) <
        0) {
        if (err_msg)
            *err_msg = "AWG_H3: invalid range";
        return -1;
    }
    if (parse_hrange_str(getenv("AWG_H4") ? getenv("AWG_H4") : "4", &cfg->h4) <
        0) {
        if (err_msg)
            *err_msg = "AWG_H4: invalid range";
        return -1;
    }

    cfg->s3 = 0;
    v = getenv("AWG_S3");
    if (v && v[0]) {
        if (parse_int_strict(v, &cfg->s3) < 0) {
            if (err_msg)
                *err_msg = "AWG_S3: invalid integer";
            return -1;
        }
    }
    cfg->s4 = 0;
    v = getenv("AWG_S4");
    if (v && v[0]) {
        if (parse_int_strict(v, &cfg->s4) < 0) {
            if (err_msg)
                *err_msg = "AWG_S4: invalid integer";
            return -1;
        }
    }
    return 0;
}

int load_obfs_profile_env(awg_config_t *cfg) {
    const char *v;
    if (!cfg)
        return -1;
    v = getenv("AWG_OBFS_PROFILE");
    cfg->obfs_profile = parse_obfs_profile(v);
    return 0;
}

int load_cps_env(awg_config_t *cfg, cps_template_t storage[5],
                 const char **err_msg) {
    const char *inames[] = {"AWG_I1", "AWG_I2", "AWG_I3", "AWG_I4", "AWG_I5"};
    const char *v;
    if (!cfg || !storage)
        return -1;

    for (int i = 0; i < 5; i++) {
        v = getenv(inames[i]);
        if (!v || !v[0])
            continue;
        if (cps_parse(v, &storage[i]) < 0) {
            if (err_msg)
                *err_msg = inames[i];
            return -1;
        }
        cfg->cps[i] = &storage[i];
    }
    return 0;
}

int merge_endpoint_values(const char *env_listen, const char *env_remote,
                          const char *file_listen, int file_have_listen,
                          const char *file_remote, int file_have_remote,
                          char *listen_out, size_t listen_out_sz,
                          char *remote_out, size_t remote_out_sz) {
    if (!listen_out || !remote_out || listen_out_sz == 0 || remote_out_sz == 0)
        return -1;

    listen_out[0] = '\0';
    remote_out[0] = '\0';

    if (env_listen && env_listen[0]) {
        strncpy(listen_out, env_listen, listen_out_sz - 1);
        listen_out[listen_out_sz - 1] = '\0';
    }
    if (env_remote && env_remote[0]) {
        strncpy(remote_out, env_remote, remote_out_sz - 1);
        remote_out[remote_out_sz - 1] = '\0';
    }

    if (file_have_listen && file_listen && file_listen[0]) {
        strncpy(listen_out, file_listen, listen_out_sz - 1);
        listen_out[listen_out_sz - 1] = '\0';
    }
    if (file_have_remote && file_remote && file_remote[0]) {
        strncpy(remote_out, file_remote, remote_out_sz - 1);
        remote_out[remote_out_sz - 1] = '\0';
    }

    return 0;
}

const char *select_dns_value(const char *env_dns, const char *file_dns,
                             int file_have_dns) {
    if (file_have_dns && file_dns && file_dns[0])
        return file_dns;
    if (env_dns && env_dns[0])
        return env_dns;
    return NULL;
}

void apply_file_obfuscation_overrides(awg_config_t *cfg,
                                      const awg_file_config_t *file_cfg) {
    if (!cfg || !file_cfg)
        return;
    if (file_cfg->have_jc)
        cfg->jc = file_cfg->jc;
    if (file_cfg->have_jmin)
        cfg->jmin = file_cfg->jmin;
    if (file_cfg->have_jmax)
        cfg->jmax = file_cfg->jmax;
    if (file_cfg->have_s1)
        cfg->s1 = file_cfg->s1;
    if (file_cfg->have_s2)
        cfg->s2 = file_cfg->s2;
    if (file_cfg->have_s3)
        cfg->s3 = file_cfg->s3;
    if (file_cfg->have_s4)
        cfg->s4 = file_cfg->s4;
    if (file_cfg->have_h1)
        cfg->h1 = file_cfg->h1;
    if (file_cfg->have_h2)
        cfg->h2 = file_cfg->h2;
    if (file_cfg->have_h3)
        cfg->h3 = file_cfg->h3;
    if (file_cfg->have_h4)
        cfg->h4 = file_cfg->h4;
}
