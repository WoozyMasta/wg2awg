#include "proxy.h"
#include "morph.h"
#include "cps.h"
#include "log.h"
#include "base64.h"
#include "config_file.h"
#include "config_runtime.h"
#include "curve25519.h"
#include "obfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#ifndef VERSION
#define VERSION "dev"
#endif

static int slen(const char *s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void print_version(void) { fputs("wg2awg " VERSION "\n", stdout); }

static void print_help(void) {
    fputs(
        "wg2awg " VERSION "\n"
        "\n"
        "Usage: wg2awg [-c <config>] [-l <level>] [-m <mode>]\n"
        "              [-L <host:port>] [-r <host:port>]\n"
        "              [-s <auto|port>] [-g] [-P <key>] [-h] [-v]\n"
        "\n"
        "AmneziaWG <-> WireGuard transparent proxy.\n"
        "Translates AWG obfuscated traffic to plain WireGuard and back.\n"
        "Designed for routers (MikroTik, OpenWrt) that run a standard WG "
        "client.\n"
        "\n"
        "Options:\n"
        "  -c, --config <path>       Load AmneziaWG/WireGuard config file\n"
        "  -l, --log-level <level>   Log level: none | error | info | debug  "
        "(default: info)\n"
        "  -m, --mode <mode>         Proxy mode: client | gateway | server\n"
        "  -L, --listen <host:port>  Local listen address override\n"
        "  -r, --remote <host:port>  Remote endpoint override\n"
        "  -s, --src-port <value>    Remote socket source port: auto | <port>\n"
        "  -h, --help                Show this help and exit\n"
        "  -v, --version             Show version and exit\n"
        "  -g, --gen-morph-key       Generate a random MorphKey and print it "
        "(base64)\n"
        "  -P, --morph-probe <key>   Print derived Morph params for key "
        "(base64 or hex)\n"
        "      [-S, --slot N]        Probe-only slot override (default: "
        "current)\n"
        "\n"
        "Config file (--config / AWG_CONFIG env var):\n"
        "  INI-format AmneziaWG client or server config.\n"
        "  Config file values take priority over environment variables.\n"
        "\n"
        "  [Interface] fields read:\n"
        "    PrivateKey    client private key; public key is derived "
        "automatically\n"
        "    ListenPort    listen port, binds to 0.0.0.0:<port>\n"
        "    Jc, Jmin, Jmax, S1-S4, H1-H4, I1-I5  AWG obfuscation parameters\n"
        "    DNS           DNS server(s), comma-separated; written to "
        "/etc/resolv.conf\n"
        "    MorphKey      wg2awg Morph extension; not an AmneziaWG parameter\n"
        "    ObfsProfile   wg2awg outer obfuscation profile; not AmneziaWG\n"
        "\n"
        "  [Peer] fields read:\n"
        "    PublicKey     remote AWG server public key\n"
        "    Endpoint      remote AWG server address (host:port)\n"
        "  Multiple [Peer] sections are supported (server mode: each peer = a "
        "WG client).\n"
        "\n"
        "Environment variables:\n"
        "\n"
        "  AWG_CONFIG              Path to config file (same as -c)\n"
        "\n"
        "  Connection:\n"
        "    AWG_LISTEN            Local listen address, host:port  (e.g. "
        "0.0.0.0:51820)\n"
        "    AWG_REMOTE            Remote AWG server address, host:port\n"
        "    AWG_MODE              Proxy mode: client (default) | gateway | "
        "server\n"
        "\n"
        "  Keys:\n"
        "    AWG_SERVER_PUB        Server public key (base64)\n"
        "    AWG_CLIENT_PUB        Client public key (base64)\n"
        "    AWG_PRIVATE_KEY       Client private key (base64); public key is "
        "derived automatically\n"
        "                          (AWG_CLIENT_PUB takes priority if both are "
        "set)\n"
        "    AWG_CLIENT_PUBS       Server mode: comma-separated client public "
        "keys\n"
        "    AWG_CLIENT_PUBS_FILE  Server mode: path to file with client "
        "public keys\n"
        "\n"
        "  AmneziaWG framing (protocol parameters):\n"
        "    AWG_JC                Junk packet count (0 = disabled)\n"
        "    AWG_JMIN              Junk packet minimum size, bytes\n"
        "    AWG_JMAX              Junk packet maximum size, bytes\n"
        "    AWG_S1                Init handshake packet padding, bytes\n"
        "    AWG_S2                Response handshake packet padding, bytes\n"
        "    AWG_H1..AWG_H4        Magic header values (uint32 or MIN-MAX "
        "range)\n"
        "    AWG_S3                v2: cookie packet padding, bytes\n"
        "    AWG_S4                v2: transport packet padding, bytes\n"
        "    AWG_I1..AWG_I5        v1.5: CPS junk injection templates\n"
        "\n"
        "  wg2awg extensions (not part of AmneziaWG):\n"
        "    The AWG_ prefix below is retained for config compatibility only.\n"
        "    AWG_OBFS_PROFILE      wg2awg proxy-to-proxy outer profile: "
        "off|stun_ice|dtls_record|rtp_media|source_query|raknet|"
        "quic_short|game_enet|game_kcp|dns_like\n"
        "    AWG_MORPH_KEY         wg2awg Morph key; replaces H/S/J params\n"
        "    AWG_MORPH_KEY_FILE    path to file containing the wg2awg "
        "MorphKey (base64)\n"
        "\n"
        "  Reconnect / timeouts:\n"
        "    AWG_TIMEOUT               Inactivity reconnect timeout, seconds "
        "(default: 180)\n"
        "    AWG_REMOTE_SILENT_TIMEOUT Remote-silent reconnect timeout, "
        "seconds (default: auto = keepalive*4, min 30, capped at "
        "EXIT_TIMEOUT/2)"
        "\n"
        "    AWG_REMOTE_SILENT_EXIT_TIMEOUT\n"
        "                              Exit after remote silent (client "
        "active) "
        "for N seconds (default: 600, 0 = disabled)\n"
        "    AWG_CONNECT_RETRIES       Initial connect attempts, 0 = unlimited "
        "(default: 0)\n"
        "    AWG_DNS_RESOLVE_FAILURE_TIMEOUT\n"
        "                              Exit after consecutive DNS resolve "
        "failures for N seconds (default: 720, 0 = disabled)\n"
        "\n"
        "  Network:\n"
        "    AWG_DNS          DNS server(s) for hostname resolution, written "
        "to /etc/resolv.conf\n"
        "    AWG_SRC_PORT     Fixed source port for remote socket (default: "
        "auto)\n"
        "    AWG_SRC_PORT_DRIFT  Rotate remote source port on recovery "
        "reconnect to bust stale NAT/conntrack: 1 on, 0 off "
        "(default: 1, auto src-port only)\n"
        "    AWG_SOCKET_BUF   UDP socket buffer size, bytes (default: "
        "16777216)\n"
        "    AWG_NO_GRO       Disable UDP GRO: 1 to disable (default: 0)\n"
        "\n"
        "  Performance (Linux-specific):\n"
        "    AWG_CPU_C2S      CPU core for client->server thread (-1 = auto)\n"
        "    AWG_CPU_S2C      CPU core for server->client thread (-1 = auto)\n"
        "    AWG_BUSY_POLL    SO_BUSY_POLL timeout, \xc2\xb5s (0 = off)\n"
        "\n"
        "  Logging:\n"
        "    AWG_LOG_LEVEL    Log level: none | error | info | debug (default: "
        "info)\n"
        "\n"
        "Examples:\n"
        "\n"
        "  # Client mode - AmneziaWG config file (minimal, all params from "
        "file):\n"
        "  wg2awg -c /etc/awg/wg0.conf\n"
        "\n"
        "  # Client mode - config file + override listen address + debug "
        "logging:\n"
        "  AWG_LISTEN=127.0.0.1:51820 wg2awg -c /etc/awg/wg0.conf -l debug\n"
        "\n"
        "  # Client mode - environment variables only:\n"
        "  AWG_LISTEN=0.0.0.0:51820 \\\n"
        "  AWG_REMOTE=vpn.example.com:51820 \\\n"
        "  AWG_SERVER_PUB=<server-base64-pubkey> \\\n"
        "  AWG_CLIENT_PUB=<client-base64-pubkey> \\\n"
        "  AWG_JC=7 AWG_JMIN=32 AWG_JMAX=324 AWG_S1=0 AWG_S2=7 \\\n"
        "  AWG_H1=1250212372 AWG_H2=322115822 AWG_H3=412530544 "
        "AWG_H4=654563364 \\\n"
        "  wg2awg\n"
        "\n"
        "  # Server mode - config file declares multiple [Peer] clients:\n"
        "  AWG_MODE=server AWG_REMOTE=127.0.0.1:51821 wg2awg -c "
        "/etc/awg/server.conf\n"
        "\n"
        "  # Server mode - environment variables only:\n"
        "  AWG_MODE=server \\\n"
        "  AWG_LISTEN=0.0.0.0:51820 \\\n"
        "  AWG_REMOTE=127.0.0.1:51821 \\\n"
        "  AWG_SERVER_PUB=<server-base64-pubkey> \\\n"
        "  AWG_CLIENT_PUBS=<client1-pubkey>,<client2-pubkey> \\\n"
        "  AWG_JC=7 AWG_JMIN=32 AWG_JMAX=324 AWG_S1=0 AWG_S2=7 \\\n"
        "  AWG_H1=1250212372 AWG_H2=322115822 AWG_H3=412530544 "
        "AWG_H4=654563364 \\\n"
        "  wg2awg\n"
        "\n"
        "Source: https://github.com/WoozyMasta/wg2awg\n",
        stdout);
}

static void fatal(const char *msg) {
    log_msg("FATAL: ", msg);
    _exit(1);
}

static proxy_t g_proxy;
static awg_config_t g_config;
static cps_template_t g_cps_storage[5];
static awg_file_config_t g_file_cfg;

/* Persistent buffers for listen/remote strings derived from config file */
static char g_listen_buf[256];
static char g_remote_buf[256];

int main(int argc, char *argv[]) {
    const char *v;
    awg_config_t *cfg = &g_config;
    memset(cfg, 0, sizeof(*cfg));

    /* -- 1. Parse CLI flags -- */
    const char *config_path = getenv("AWG_CONFIG");
    const char *log_level_flag = NULL;
    const char *mode_flag = NULL;
    const char *listen_flag = NULL;
    const char *remote_flag = NULL;
    const char *src_port_flag = NULL;
    int morph_gen_key_flag = 0;
    const char *morph_probe_key = NULL;
    int64_t morph_probe_slot = -1;

    for (int i = 1; i < argc; i++) {
        /* config */
        if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) &&
            i + 1 < argc) {
            config_path = argv[++i];
        } else if (strncmp(argv[i], "--config=", 9) == 0) {
            config_path = argv[i] + 9;

            /* log level */
        } else if ((strcmp(argv[i], "-l") == 0 ||
                    strcmp(argv[i], "--log-level") == 0) &&
                   i + 1 < argc) {
            log_level_flag = argv[++i];
        } else if (strncmp(argv[i], "--log-level=", 12) == 0) {
            log_level_flag = argv[i] + 12;

            /* mode */
        } else if ((strcmp(argv[i], "-m") == 0 ||
                    strcmp(argv[i], "--mode") == 0) &&
                   i + 1 < argc) {
            mode_flag = argv[++i];
        } else if (strncmp(argv[i], "--mode=", 7) == 0) {
            mode_flag = argv[i] + 7;

            /* listen */
        } else if ((strcmp(argv[i], "-L") == 0 ||
                    strcmp(argv[i], "--listen") == 0) &&
                   i + 1 < argc) {
            listen_flag = argv[++i];
        } else if (strncmp(argv[i], "--listen=", 9) == 0) {
            listen_flag = argv[i] + 9;

            /* remote */
        } else if ((strcmp(argv[i], "-r") == 0 ||
                    strcmp(argv[i], "--remote") == 0) &&
                   i + 1 < argc) {
            remote_flag = argv[++i];
        } else if (strncmp(argv[i], "--remote=", 9) == 0) {
            remote_flag = argv[i] + 9;

            /* source port */
        } else if ((strcmp(argv[i], "-s") == 0 ||
                    strcmp(argv[i], "--src-port") == 0) &&
                   i + 1 < argc) {
            src_port_flag = argv[++i];
        } else if (strncmp(argv[i], "--src-port=", 11) == 0) {
            src_port_flag = argv[i] + 11;

            /* help */
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;

            /* version */
        } else if (strcmp(argv[i], "-v") == 0 ||
                   strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;

            /* gen-morph-key: output a CSPRNG base64 key and exit */
        } else if (strcmp(argv[i], "-g") == 0 ||
                   strcmp(argv[i], "--gen-morph-key") == 0) {
            morph_gen_key_flag = 1;

            /* morph-probe <key> [--slot N]: print derived params and exit */
        } else if ((strcmp(argv[i], "-P") == 0 ||
                    strcmp(argv[i], "--morph-probe") == 0) &&
                   i + 1 < argc) {
            morph_probe_key = argv[++i];
        } else if ((strcmp(argv[i], "-S") == 0 ||
                    strcmp(argv[i], "--slot") == 0) &&
                   i + 1 < argc) {
            char *end = NULL;
            errno = 0;
            morph_probe_slot = (int64_t)strtoll(argv[++i], &end, 10);
            if (errno == ERANGE || !end || *end != '\0' ||
                morph_probe_slot < 0) {
                log_msg("FATAL: ", "--slot: expected non-negative integer");
                return 1;
            }
        } else {
            const char *parts[] = {"unknown option: ", argv[i]};
            log_msgn("FATAL: ", parts, 2);
            print_help();
            return 1;
        }
    }

    if (morph_probe_slot >= 0 && !morph_probe_key) {
        log_msg("FATAL: ", "--slot requires --morph-probe");
        return 1;
    }

    /* Early-exit utility flags (no config needed) */
    if (morph_gen_key_flag) {
        morph_gen_key();
        return 0; /* morph_gen_key calls _exit on error, so this is clean exit
                   */
    }
    if (morph_probe_key) {
        morph_probe(morph_probe_key, morph_probe_slot);
        return 0;
    }

    int cfg_file_loaded = 0;
    if (config_path && config_path[0]) {
        if (config_file_parse(config_path, &g_file_cfg) < 0)
            _exit(1);
        cfg_file_loaded = 1;
    }

    /* -- 2. Log level: CLI flag > AWG_LOG_LEVEL env > default info -- */
    cfg->log_level = LOG_INFO;
    {
        /* CLI flag takes priority over env var */
        const char *ll =
            log_level_flag ? log_level_flag : getenv("AWG_LOG_LEVEL");
        int parsed_level;
        if (parse_log_level(ll, &parsed_level) == 0)
            cfg->log_level = parsed_level;
    }
    g_log_level = cfg->log_level;

    /* -- 3. AWG mode -- */
    cfg->mode = AWG_MODE_CLIENT;
    v = getenv("AWG_MODE");
    if (v && v[0]) {
        if (parse_awg_mode(v, &cfg->mode) < 0)
            fatal("AWG_MODE: expected client|gateway|server");
    }
    if (mode_flag && mode_flag[0]) {
        if (parse_awg_mode(mode_flag, &cfg->mode) < 0)
            fatal("--mode: expected client|gateway|server");
    }

    /* -- 4. Listen / remote addresses (env var as default, config file
     * overrides) -- */
    if (merge_endpoint_values(getenv("AWG_LISTEN"), getenv("AWG_REMOTE"),
                              cfg_file_loaded ? g_file_cfg.listen : NULL,
                              cfg_file_loaded ? g_file_cfg.have_listen : 0,
                              cfg_file_loaded ? g_file_cfg.endpoint : NULL,
                              cfg_file_loaded ? g_file_cfg.have_endpoint : 0,
                              g_listen_buf, sizeof(g_listen_buf), g_remote_buf,
                              sizeof(g_remote_buf)) < 0)
        fatal("failed to merge listen/remote config");

    if (listen_flag && listen_flag[0]) {
        strncpy(g_listen_buf, listen_flag, sizeof(g_listen_buf) - 1);
        g_listen_buf[sizeof(g_listen_buf) - 1] = '\0';
    }
    if (remote_flag && remote_flag[0]) {
        strncpy(g_remote_buf, remote_flag, sizeof(g_remote_buf) - 1);
        g_remote_buf[sizeof(g_remote_buf) - 1] = '\0';
    }

    const char *listen_str = g_listen_buf;
    const char *remote_str = g_remote_buf;

    /* -- 5. AmneziaWG framing + wg2awg outer profile -- */
    {
        const char *obf_err = NULL;
        if (load_obfuscation_env(cfg, &obf_err) < 0)
            fatal(obf_err ? obf_err : "invalid obfuscation env");
        if (load_obfs_profile_env(cfg) < 0)
            fatal("failed to load AWG_OBFS_PROFILE");
    }

    /* Override with config file values (config has priority) */
    if (cfg_file_loaded)
        apply_file_obfuscation_overrides(cfg, &g_file_cfg);
    if (cfg_file_loaded)
        apply_file_obfs_profile_override(cfg, &g_file_cfg);

    if (cfg->jc > 0 && cfg->jmin == cfg->jmax && cfg->jmax < 65535)
        cfg->jmax++;

    /* -- 5b. Morph key (env defaults, config file priority) -- */
    {
        const char *morph_err = NULL;
        if (load_morph_key_env(cfg, &morph_err) < 0)
            fatal(morph_err ? morph_err : "invalid morph key env");
    }
    if (cfg_file_loaded)
        apply_file_morph_override(cfg, &g_file_cfg);

    /* -- 6. Public keys (env var default, config file priority) -- */
    /* Server public key (remote AWG server) */
    v = getenv("AWG_SERVER_PUB");
    if (v && v[0]) {
        if (base64_decode(v, slen(v), cfg->server_pub, 32) != 32)
            fatal("AWG_SERVER_PUB: must decode to 32 bytes");
    }
    if (cfg_file_loaded && g_file_cfg.have_server_pub)
        memcpy(cfg->server_pub, g_file_cfg.server_pub, 32);

    /* Client public key (local WG client) */
    const char *cpub_str = getenv("AWG_CLIENT_PUB");
    const char *cpriv_str = getenv("AWG_PRIVATE_KEY");
    const char *cpubs_str = getenv("AWG_CLIENT_PUBS");
    const char *cpubs_file_str = getenv("AWG_CLIENT_PUBS_FILE");

    if (cpriv_str && cpriv_str[0]) {
        uint8_t priv[32];
        if (base64_decode(cpriv_str, slen(cpriv_str), priv, 32) != 32)
            fatal("AWG_PRIVATE_KEY: must decode to 32 bytes");
        curve25519_public_key(cfg->client_pub, priv);
    }
    if (cpub_str && cpub_str[0]) {
        if (base64_decode(cpub_str, slen(cpub_str), cfg->client_pub, 32) != 32)
            fatal("AWG_CLIENT_PUB: must decode to 32 bytes");
    }
    if (cfg_file_loaded && g_file_cfg.have_client_pub)
        memcpy(cfg->client_pub, g_file_cfg.client_pub, 32);

    /* Server mode: peer public keys from env */
    if (cfg->mode == AWG_MODE_SERVER) {
        if (cpubs_str && cpubs_str[0])
            if (parse_server_peer_list(cfg, cpubs_str) < 0)
                fatal("AWG_CLIENT_PUBS: invalid client public key list");
        if (cpubs_file_str && cpubs_file_str[0])
            if (load_server_peer_file(cfg, cpubs_file_str) < 0)
                fatal(
                    "AWG_CLIENT_PUBS_FILE: cannot parse public key list file");
        /* Also add peer pubs from config file (each [Peer] is a client) */
        if (cfg_file_loaded) {
            for (int i = 0; i < g_file_cfg.peer_pub_count; i++)
                if (add_server_peer_pub_unique(cfg, g_file_cfg.peer_pubs[i]) <
                    0)
                    fatal("config [Peer]: too many peers (max 256)");
        }
    }

    /* -- 6b. Morph Mode: fail-closed + derive static params -- */
    if (cfg->morph_enabled) {
        if (morph_obfuscation_env_conflict() ||
            (cfg_file_loaded && morph_obfuscation_file_conflict(&g_file_cfg)))
            fatal("MorphKey: cannot be combined with explicit "
                  "H1-H4/S1-S4/Jc/Jmin/Jmax parameters");
        morph_derive_static(cfg, cfg->morph_key);
    }

    /* -- 7. Validate merged config -- */
    {
        const char *cfg_err = NULL;
        if (config_validate(cfg, &cfg_err) < 0)
            fatal(cfg_err);
    }

    /* Compute MAC1 keys and derived fields */
    config_compute(cfg);

    /* Required field checks (after merge + compute) */
    {
        int errs = 0;
        if (!listen_str[0]) {
            log_msg(
                "FATAL: ",
                "listen address not set (AWG_LISTEN or ListenPort in config)");
            errs++;
        }
        if (!remote_str[0]) {
            log_msg("FATAL: ", "remote address not set (AWG_REMOTE or [Peer] "
                               "Endpoint in config)");
            errs++;
        }
        if (!cfg->has_server_pub) {
            log_msg("FATAL: ", "server public key not set (AWG_SERVER_PUB or "
                               "[Peer] PublicKey in config)");
            errs++;
        }
        if (cfg->mode != AWG_MODE_SERVER && !cfg->has_client_pub) {
            log_msg("FATAL: ", "client public key not set (AWG_CLIENT_PUB or "
                               "PrivateKey in config)");
            errs++;
        }
        if (cfg->mode == AWG_MODE_SERVER && !cfg->has_client_pub &&
            cfg->server_peer_count == 0) {
            log_msg("FATAL: ",
                    "server mode requires client public key(s) "
                    "(AWG_CLIENT_PUB / AWG_CLIENT_PUBS / [Peer] in config)");
            errs++;
        }
        if (errs > 0)
            _exit(1);
    }

    /* -- 8. CPS templates I1-I5 (env defaults, config file priority) -- */
    {
        const char *cps_err = NULL;
        if (load_cps_env(cfg, g_cps_storage, &cps_err) < 0) {
            const char *eparts[] = {cps_err ? cps_err : "AWG_Ix",
                                    ": invalid CPS template"};
            log_msgn("FATAL: ", eparts, 2);
            _exit(1);
        }
    }
    if (cfg_file_loaded) {
        for (int i = 0; i < 5; i++) {
            if (g_file_cfg.have_cps[i])
                cfg->cps[i] = &g_file_cfg.cps[i];
        }
    }

    /* -- 9. Operational parameters (env only) -- */
    {
        const char *op_err = NULL;
        if (load_operational_env(cfg, &op_err) < 0)
            fatal(op_err ? op_err : "invalid operational env");
    }

    /* Resolve auto remote-silent timeout from PersistentKeepalive (env wins) */
    cfg->remote_silent_timeout = compute_remote_silent_timeout(
        cfg->remote_silent_timeout,
        cfg_file_loaded && g_file_cfg.have_keepalive,
        cfg_file_loaded ? g_file_cfg.keepalive : 0,
        cfg->remote_silent_exit_timeout);

    {
        const char *np_err = NULL;
        if (load_network_perf_env(cfg, &np_err) < 0)
            fatal(np_err ? np_err : "invalid network/perf env");
    }
    if (src_port_flag && src_port_flag[0]) {
        int parsed_src_port;
        if (parse_src_port(src_port_flag, &parsed_src_port) < 0)
            fatal("--src-port: expected auto or integer");
        cfg->src_port = parsed_src_port;
    }
    int src_port = cfg->src_port;

    /* -- 10. DNS resolver -- */
    {
        const char *dns_str = select_dns_value(
            getenv("AWG_DNS"), cfg_file_loaded ? g_file_cfg.dns : NULL,
            cfg_file_loaded ? g_file_cfg.have_dns : 0);
        if (dns_str) {
            (void)write_dns_resolv_conf("/etc/resolv.conf", dns_str);
            log_info2("DNS: ", dns_str);
        }
    }

    /* -- 11. Startup log -- */
    if (cfg->morph_enabled) {
        char h4b[12], s4b[12];
        const char *parts[] = {"morph: enabled H4=0x",
                               u32_to_str(h4b, cfg->h4.min),
                               " S4=", u32_to_str(s4b, cfg->s4)};
        log_infon(parts, 4);
    }
    {
        const char *mode_str =
            cfg->morph_enabled ? "morph"
            : cfg->s3 > 0 || cfg->s4 > 0 || cfg->h1.min != cfg->h1.max ||
                    cfg->h2.min != cfg->h2.max || cfg->h3.min != cfg->h3.max ||
                    cfg->h4.min != cfg->h4.max
                ? "v2"
            : (cfg->cps[0] || cfg->cps[1] || cfg->cps[2] || cfg->cps[3] ||
               cfg->cps[4])
                ? "v1.5"
                : "v1";
        const char *awg_mode_str = cfg->mode == AWG_MODE_GATEWAY  ? "gateway"
                                   : cfg->mode == AWG_MODE_SERVER ? "server"
                                                                  : "client";
        const char *parts[] = {
            "wg2awg ",         VERSION,
            " linux/c proto=", mode_str,
            " awg_mode=",      awg_mode_str,
            " obfs=",          obfs_profile_name(cfg->obfs_profile)};
        log_infon(parts, 8);
    }
    if (cfg_file_loaded) {
        log_info2("config file: ", config_path);
    }
    {
        char spb[12];
        const char *parts[] = {
            "listen=",    listen_str,
            " remote=",   remote_str,
            " src_port=", src_port > 0 ? u32_to_str(spb, src_port) : "auto"};
        log_infon(parts, 6);
    }
    {
        char jcb[12], jminb[12], jmaxb[12];
        const char *parts[] = {"config: JC=", u32_to_str(jcb, cfg->jc),
                               " JMIN=",      u32_to_str(jminb, cfg->jmin),
                               " JMAX=",      u32_to_str(jmaxb, cfg->jmax)};
        log_infon(parts, 6);
    }
    {
        char s1b[12], s2b[12], s3b[12], s4b[12];
        const char *parts[] = {"config: S1=", u32_to_str(s1b, cfg->s1),
                               " S2=",        u32_to_str(s2b, cfg->s2),
                               " S3=",        u32_to_str(s3b, cfg->s3),
                               " S4=",        u32_to_str(s4b, cfg->s4)};
        log_infon(parts, 8);
    }
    {
        char h1minb[12], h1maxb[12], h2minb[12], h2maxb[12];
        char h3minb[12], h3maxb[12], h4minb[12], h4maxb[12];
        if (cfg->h1.min == cfg->h1.max && cfg->h2.min == cfg->h2.max &&
            cfg->h3.min == cfg->h3.max && cfg->h4.min == cfg->h4.max) {
            const char *parts[] = {
                "config: H1=", u32_to_str(h1minb, cfg->h1.min),
                " H2=",        u32_to_str(h2minb, cfg->h2.min),
                " H3=",        u32_to_str(h3minb, cfg->h3.min),
                " H4=",        u32_to_str(h4minb, cfg->h4.min)};
            log_infon(parts, 8);
        } else {
            const char *parts[] = {
                "config: H1=", u32_to_str(h1minb, cfg->h1.min),
                "-",           u32_to_str(h1maxb, cfg->h1.max),
                " H2=",        u32_to_str(h2minb, cfg->h2.min),
                "-",           u32_to_str(h2maxb, cfg->h2.max),
                " H3=",        u32_to_str(h3minb, cfg->h3.min),
                "-",           u32_to_str(h3maxb, cfg->h3.max),
                " H4=",        u32_to_str(h4minb, cfg->h4.min),
                "-",           u32_to_str(h4maxb, cfg->h4.max)};
            log_infon(parts, 16);
        }
    }
    if (cfg->no_gro)
        log_info("config: UDP GRO disabled (AWG_NO_GRO=1)");
    if (cfg->cpu_c2s >= 0 || cfg->cpu_s2c >= 0 || cfg->busy_poll > 0) {
        char c2sb[12], s2cb[12], bpb[12];
        const char *parts[] = {
            "perf: cpu_c2s=",
            cfg->cpu_c2s >= 0 ? u32_to_str(c2sb, cfg->cpu_c2s) : "auto",
            " cpu_s2c=",
            cfg->cpu_s2c >= 0 ? u32_to_str(s2cb, cfg->cpu_s2c) : "auto",
            " busy_poll=",
            cfg->busy_poll > 0 ? u32_to_str(bpb, cfg->busy_poll) : "off"};
        log_infon(parts, 6);
    }

    /* -- 12. Init and run -- */
    if (proxy_init(&g_proxy, cfg, listen_str, remote_str, src_port) < 0)
        fatal("proxy init failed");

    return proxy_run(&g_proxy);
}
