#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "test.h"
#include "config_runtime.h"

static void test_mode_client(void) {
    int mode = -1;
    ASSERT_EQ(parse_awg_mode("client", &mode), 0);
    ASSERT_EQ(mode, AWG_MODE_CLIENT);
}

static void test_mode_gateway(void) {
    int mode = -1;
    ASSERT_EQ(parse_awg_mode("gateway", &mode), 0);
    ASSERT_EQ(mode, AWG_MODE_GATEWAY);
}

static void test_mode_server(void) {
    int mode = -1;
    ASSERT_EQ(parse_awg_mode("server", &mode), 0);
    ASSERT_EQ(mode, AWG_MODE_SERVER);
}

static void test_mode_case_insensitive(void) {
    int mode = -1;
    ASSERT_EQ(parse_awg_mode("ClIeNt", &mode), 0);
    ASSERT_EQ(mode, AWG_MODE_CLIENT);
    ASSERT_EQ(parse_awg_mode("GATEWAY", &mode), 0);
    ASSERT_EQ(mode, AWG_MODE_GATEWAY);
}

static void test_mode_invalid(void) {
    int mode = -1;
    ASSERT_EQ(parse_awg_mode(NULL, &mode), -1);
    ASSERT_EQ(parse_awg_mode("", &mode), -1);
    ASSERT_EQ(parse_awg_mode("foo", &mode), -1);
    ASSERT_EQ(parse_awg_mode("normal", &mode), -1);
    ASSERT_EQ(parse_awg_mode("reverse", &mode), -1);
    ASSERT_EQ(parse_awg_mode("server", NULL), -1);
}

static void test_log_level_valid(void) {
    int level = -1;
    ASSERT_EQ(parse_log_level("none", &level), 0);
    ASSERT_EQ(level, LOG_NONE);
    ASSERT_EQ(parse_log_level("error", &level), 0);
    ASSERT_EQ(level, LOG_ERROR);
    ASSERT_EQ(parse_log_level("info", &level), 0);
    ASSERT_EQ(level, LOG_INFO);
    ASSERT_EQ(parse_log_level("debug", &level), 0);
    ASSERT_EQ(level, LOG_DEBUG);
}

static void test_log_level_case_insensitive(void) {
    int level = -1;
    ASSERT_EQ(parse_log_level("None", &level), 0);
    ASSERT_EQ(level, LOG_NONE);
    ASSERT_EQ(parse_log_level("DEBUG", &level), 0);
    ASSERT_EQ(level, LOG_DEBUG);
}

static void test_log_level_invalid(void) {
    int level = -1;
    ASSERT_EQ(parse_log_level(NULL, &level), -1);
    ASSERT_EQ(parse_log_level("", &level), -1);
    ASSERT_EQ(parse_log_level("verbose", &level), -1);
    ASSERT_EQ(parse_log_level("info", NULL), -1);
}

static void test_hrange_value(void) {
    hrange_t r = {0, 0};
    ASSERT_EQ(parse_hrange_str("123", &r), 0);
    ASSERT_EQ(r.min, 123u);
    ASSERT_EQ(r.max, 123u);
}

static void test_hrange_range(void) {
    hrange_t r = {0, 0};
    ASSERT_EQ(parse_hrange_str("100-200", &r), 0);
    ASSERT_EQ(r.min, 100u);
    ASSERT_EQ(r.max, 200u);
}

static void test_hrange_invalid(void) {
    hrange_t r = {0, 0};
    ASSERT_EQ(parse_hrange_str(NULL, &r), -1);
    ASSERT_EQ(parse_hrange_str("", &r), -1);
    ASSERT_EQ(parse_hrange_str("abc", &r), -1);
    ASSERT_EQ(parse_hrange_str("200-100", &r), -1);
    ASSERT_EQ(parse_hrange_str("-100", &r), -1);
    ASSERT_EQ(parse_hrange_str("100-", &r), -1);
    ASSERT_EQ(parse_hrange_str("1-2-3", &r), -1);
    ASSERT_EQ(parse_hrange_str("4294967296", &r), -1);
    ASSERT_EQ(parse_hrange_str("1", NULL), -1);
}

static void test_parse_int_strict_valid(void) {
    int v = 0;
    ASSERT_EQ(parse_int_strict("0", &v), 0);
    ASSERT_EQ(v, 0);
    ASSERT_EQ(parse_int_strict("-42", &v), 0);
    ASSERT_EQ(v, -42);
    ASSERT_EQ(parse_int_strict("123456", &v), 0);
    ASSERT_EQ(v, 123456);
}

static void test_parse_int_strict_invalid(void) {
    int v = 0;
    ASSERT_EQ(parse_int_strict(NULL, &v), -1);
    ASSERT_EQ(parse_int_strict("", &v), -1);
    ASSERT_EQ(parse_int_strict("12x", &v), -1);
    ASSERT_EQ(parse_int_strict("x12", &v), -1);
    ASSERT_EQ(parse_int_strict("999999999999999999999", &v), -1);
    ASSERT_EQ(parse_int_strict("1", NULL), -1);
}

static void test_parse_src_port_valid(void) {
    int p = -1;
    ASSERT_EQ(parse_src_port("auto", &p), 0);
    ASSERT_EQ(p, 0);
    ASSERT_EQ(parse_src_port("51820", &p), 0);
    ASSERT_EQ(p, 51820);
    ASSERT_EQ(parse_src_port("-1", &p), 0);
    ASSERT_EQ(p, -1);
}

static void test_parse_src_port_invalid(void) {
    int p = 0;
    ASSERT_EQ(parse_src_port(NULL, &p), -1);
    ASSERT_EQ(parse_src_port("", &p), -1);
    ASSERT_EQ(parse_src_port("autox", &p), -1);
    ASSERT_EQ(parse_src_port("12x", &p), -1);
    ASSERT_EQ(parse_src_port("1", NULL), -1);
}

static void test_write_dns_resolv_conf_basic(void) {
    const char *path = "/tmp/test_resolv_conf_basic.txt";
    FILE *f;
    char buf[256];

    ASSERT_EQ(write_dns_resolv_conf(path, "1.1.1.1,8.8.8.8"), 0);
    f = fopen(path, "r");
    ASSERT(f != NULL);
    ASSERT(fgets(buf, sizeof(buf), f) != NULL);
    ASSERT(strcmp(buf, "nameserver 1.1.1.1\n") == 0);
    ASSERT(fgets(buf, sizeof(buf), f) != NULL);
    ASSERT(strcmp(buf, "nameserver 8.8.8.8\n") == 0);
    fclose(f);
    remove(path);
}

static void test_write_dns_resolv_conf_spaces(void) {
    const char *path = "/tmp/test_resolv_conf_spaces.txt";
    FILE *f;
    char buf[256];

    ASSERT_EQ(write_dns_resolv_conf(path, " 9.9.9.9,  8.8.4.4\t1.0.0.1 "), 0);
    f = fopen(path, "r");
    ASSERT(f != NULL);
    ASSERT(fgets(buf, sizeof(buf), f) != NULL);
    ASSERT(strcmp(buf, "nameserver 9.9.9.9\n") == 0);
    ASSERT(fgets(buf, sizeof(buf), f) != NULL);
    ASSERT(strcmp(buf, "nameserver 8.8.4.4\n") == 0);
    ASSERT(fgets(buf, sizeof(buf), f) != NULL);
    ASSERT(strcmp(buf, "nameserver 1.0.0.1\n") == 0);
    fclose(f);
    remove(path);
}

static void test_write_dns_resolv_conf_invalid(void) {
    ASSERT_EQ(write_dns_resolv_conf(NULL, "1.1.1.1"), -1);
    ASSERT_EQ(write_dns_resolv_conf("", "1.1.1.1"), -1);
    ASSERT_EQ(write_dns_resolv_conf("/tmp/x", NULL), -1);
    ASSERT_EQ(write_dns_resolv_conf("/tmp/x", ""), -1);
}

static void test_parse_server_peer_list_valid_and_dedup(void) {
    awg_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    ASSERT_EQ(parse_server_peer_list(
                  &cfg, "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=,"
                        "AgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA= "
                        "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="),
              0);
    ASSERT_EQ(cfg.server_peer_count, 2);
}

static void test_parse_server_peer_list_invalid(void) {
    awg_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    ASSERT_EQ(parse_server_peer_list(
                  NULL, "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="),
              -1);
    ASSERT_EQ(parse_server_peer_list(&cfg, NULL), -1);
    ASSERT_EQ(parse_server_peer_list(&cfg, "not_base64"), -1);
}

static void test_load_server_peer_file_valid(void) {
    const char *path = "/tmp/test_server_peers.txt";
    FILE *f;
    awg_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    f = fopen(path, "wb");
    ASSERT(f != NULL);
    ASSERT(fputs("AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n", f) >= 0);
    ASSERT(fputs("AgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n", f) >= 0);
    ASSERT(fclose(f) == 0);

    ASSERT_EQ(load_server_peer_file(&cfg, path), 0);
    ASSERT_EQ(cfg.server_peer_count, 2);
    remove(path);
}

static void unset_operational_env(void) {
    unsetenv("AWG_TIMEOUT");
    unsetenv("AWG_REMOTE_SILENT_TIMEOUT");
    unsetenv("AWG_REMOTE_SILENT_EXIT_TIMEOUT");
    unsetenv("AWG_CONNECT_RETRIES");
    unsetenv("AWG_DNS_RESOLVE_FAILURE_TIMEOUT");
    unsetenv("AWG_SOCKET_BUF");
}

static void test_load_operational_env_defaults(void) {
    awg_config_t cfg;
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));
    unset_operational_env();
    ASSERT_EQ(load_operational_env(&cfg, &err), 0);
    ASSERT_EQ(cfg.timeout, 180);
    ASSERT_EQ(cfg.remote_silent_timeout, 0); /* 0 = auto (derived later) */
    ASSERT_EQ(cfg.remote_silent_exit_timeout, 600);
    ASSERT_EQ(cfg.connect_retries, 0);
    ASSERT_EQ(cfg.dns_resolve_failure_timeout, 12 * 60);
    ASSERT_EQ(cfg.socket_buf, 16 * 1024 * 1024);
}

static void test_load_operational_env_valid(void) {
    awg_config_t cfg;
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));
    ASSERT_EQ(setenv("AWG_TIMEOUT", "10", 1), 0);
    ASSERT_EQ(setenv("AWG_REMOTE_SILENT_TIMEOUT", "20", 1), 0);
    ASSERT_EQ(setenv("AWG_REMOTE_SILENT_EXIT_TIMEOUT", "1200", 1), 0);
    ASSERT_EQ(setenv("AWG_CONNECT_RETRIES", "3", 1), 0);
    ASSERT_EQ(setenv("AWG_DNS_RESOLVE_FAILURE_TIMEOUT", "90", 1), 0);
    ASSERT_EQ(setenv("AWG_SOCKET_BUF", "8192", 1), 0);
    ASSERT_EQ(load_operational_env(&cfg, &err), 0);
    ASSERT_EQ(cfg.timeout, 10);
    ASSERT_EQ(cfg.remote_silent_timeout, 20);
    ASSERT_EQ(cfg.remote_silent_exit_timeout, 1200);
    ASSERT_EQ(cfg.connect_retries, 3);
    ASSERT_EQ(cfg.dns_resolve_failure_timeout, 90);
    ASSERT_EQ(cfg.socket_buf, 8192);
    unset_operational_env();
}

static void test_compute_remote_silent_timeout(void) {
    /* explicit value wins, no bounds applied */
    ASSERT_EQ(compute_remote_silent_timeout(20, 1, 25, 900), 20);
    ASSERT_EQ(compute_remote_silent_timeout(5, 0, 0, 900), 5);
    /* keepalive * 4 (exit/2 cap not reached) */
    ASSERT_EQ(compute_remote_silent_timeout(0, 1, 15, 900), 60);
    ASSERT_EQ(compute_remote_silent_timeout(0, 1, 25, 900), 100);
    /* floor 30 */
    ASSERT_EQ(compute_remote_silent_timeout(0, 1, 5, 900), 30);
    /* no keepalive -> fallback 15*4 = 60 */
    ASSERT_EQ(compute_remote_silent_timeout(0, 0, 0, 900), 60);
    /* no upper bound when exit guard disabled: keepalive 50 -> 200 */
    ASSERT_EQ(compute_remote_silent_timeout(0, 1, 50, 0), 200);
    /* capped at exit/2: keepalive 300 -> 1200, exit 900 -> 450 */
    ASSERT_EQ(compute_remote_silent_timeout(0, 1, 300, 900), 450);
    /* cap floored at 30 for tiny exit: keepalive 300, exit 40 -> 30 */
    ASSERT_EQ(compute_remote_silent_timeout(0, 1, 300, 40), 30);
}

static void test_load_operational_env_invalid(void) {
    awg_config_t cfg;
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));

    ASSERT_EQ(setenv("AWG_TIMEOUT", "x", 1), 0);
    ASSERT_EQ(load_operational_env(&cfg, &err), -1);
    ASSERT(err && strcmp(err, "AWG_TIMEOUT: invalid integer") == 0);
    unset_operational_env();

    ASSERT_EQ(setenv("AWG_CONNECT_RETRIES", "-1", 1), 0);
    err = NULL;
    ASSERT_EQ(load_operational_env(&cfg, &err), -1);
    ASSERT(err && strcmp(err, "AWG_CONNECT_RETRIES: must be >= 0") == 0);
    unset_operational_env();

    ASSERT_EQ(setenv("AWG_DNS_RESOLVE_FAILURE_TIMEOUT", "-1", 1), 0);
    err = NULL;
    ASSERT_EQ(load_operational_env(&cfg, &err), -1);
    ASSERT(err &&
           strcmp(err, "AWG_DNS_RESOLVE_FAILURE_TIMEOUT: must be >= 0") == 0);
    unset_operational_env();

    ASSERT_EQ(setenv("AWG_REMOTE_SILENT_EXIT_TIMEOUT", "-1", 1), 0);
    err = NULL;
    ASSERT_EQ(load_operational_env(&cfg, &err), -1);
    ASSERT(err &&
           strcmp(err, "AWG_REMOTE_SILENT_EXIT_TIMEOUT: must be >= 0") == 0);
    unset_operational_env();
}

static void unset_network_perf_env(void) {
    unsetenv("AWG_SRC_PORT");
    unsetenv("AWG_SRC_PORT_DRIFT");
    unsetenv("AWG_CPU_C2S");
    unsetenv("AWG_CPU_S2C");
    unsetenv("AWG_BUSY_POLL");
    unsetenv("AWG_NO_GRO");
}

static void test_load_network_perf_env_defaults(void) {
    awg_config_t cfg;
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));
    unset_network_perf_env();
    ASSERT_EQ(load_network_perf_env(&cfg, &err), 0);
    ASSERT_EQ(cfg.src_port, 0);
    ASSERT_EQ(cfg.src_port_drift, 1);
    ASSERT_EQ(cfg.cpu_c2s, -1);
    ASSERT_EQ(cfg.cpu_s2c, -1);
    ASSERT_EQ(cfg.busy_poll, 0);
    ASSERT_EQ(cfg.no_gro, 0);
}

static void test_load_network_perf_env_valid(void) {
    awg_config_t cfg;
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));
    ASSERT_EQ(setenv("AWG_SRC_PORT", "51820", 1), 0);
    ASSERT_EQ(setenv("AWG_SRC_PORT_DRIFT", "0", 1), 0);
    ASSERT_EQ(setenv("AWG_CPU_C2S", "1", 1), 0);
    ASSERT_EQ(setenv("AWG_CPU_S2C", "2", 1), 0);
    ASSERT_EQ(setenv("AWG_BUSY_POLL", "50", 1), 0);
    ASSERT_EQ(setenv("AWG_NO_GRO", "1", 1), 0);
    ASSERT_EQ(load_network_perf_env(&cfg, &err), 0);
    ASSERT_EQ(cfg.src_port, 51820);
    ASSERT_EQ(cfg.src_port_drift, 0);
    ASSERT_EQ(cfg.cpu_c2s, 1);
    ASSERT_EQ(cfg.cpu_s2c, 2);
    ASSERT_EQ(cfg.busy_poll, 50);
    ASSERT_EQ(cfg.no_gro, 1);
    unset_network_perf_env();
}

static void test_load_network_perf_env_invalid(void) {
    awg_config_t cfg;
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));

    ASSERT_EQ(setenv("AWG_SRC_PORT", "x", 1), 0);
    ASSERT_EQ(load_network_perf_env(&cfg, &err), -1);
    ASSERT(err && strcmp(err, "AWG_SRC_PORT: invalid integer") == 0);
    unset_network_perf_env();

    ASSERT_EQ(setenv("AWG_CPU_C2S", "x", 1), 0);
    err = NULL;
    ASSERT_EQ(load_network_perf_env(&cfg, &err), -1);
    ASSERT(err && strcmp(err, "AWG_CPU_C2S: invalid integer") == 0);
    unset_network_perf_env();
}

static void unset_obf_env(void) {
    unsetenv("AWG_JC");
    unsetenv("AWG_JMIN");
    unsetenv("AWG_JMAX");
    unsetenv("AWG_S1");
    unsetenv("AWG_S2");
    unsetenv("AWG_S3");
    unsetenv("AWG_S4");
    unsetenv("AWG_H1");
    unsetenv("AWG_H2");
    unsetenv("AWG_H3");
    unsetenv("AWG_H4");
}

static void test_load_obfuscation_env_defaults(void) {
    awg_config_t cfg;
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));
    unset_obf_env();
    ASSERT_EQ(load_obfuscation_env(&cfg, &err), 0);
    ASSERT_EQ(cfg.jc, 0);
    ASSERT_EQ(cfg.jmin, 0);
    ASSERT_EQ(cfg.jmax, 0);
    ASSERT_EQ(cfg.s1, 0);
    ASSERT_EQ(cfg.s2, 0);
    ASSERT_EQ(cfg.s3, 0);
    ASSERT_EQ(cfg.s4, 0);
    ASSERT_EQ(cfg.h1.min, 1u);
    ASSERT_EQ(cfg.h1.max, 1u);
    ASSERT_EQ(cfg.h4.min, 4u);
    ASSERT_EQ(cfg.h4.max, 4u);
}

static void test_load_obfuscation_env_valid(void) {
    awg_config_t cfg;
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));
    ASSERT_EQ(setenv("AWG_JC", "7", 1), 0);
    ASSERT_EQ(setenv("AWG_JMIN", "32", 1), 0);
    ASSERT_EQ(setenv("AWG_JMAX", "324", 1), 0);
    ASSERT_EQ(setenv("AWG_S1", "1", 1), 0);
    ASSERT_EQ(setenv("AWG_S2", "2", 1), 0);
    ASSERT_EQ(setenv("AWG_S3", "3", 1), 0);
    ASSERT_EQ(setenv("AWG_S4", "4", 1), 0);
    ASSERT_EQ(setenv("AWG_H1", "10-20", 1), 0);
    ASSERT_EQ(setenv("AWG_H2", "21", 1), 0);
    ASSERT_EQ(setenv("AWG_H3", "22", 1), 0);
    ASSERT_EQ(setenv("AWG_H4", "23", 1), 0);
    ASSERT_EQ(load_obfuscation_env(&cfg, &err), 0);
    ASSERT_EQ(cfg.jc, 7);
    ASSERT_EQ(cfg.jmin, 32);
    ASSERT_EQ(cfg.jmax, 324);
    ASSERT_EQ(cfg.s1, 1);
    ASSERT_EQ(cfg.s2, 2);
    ASSERT_EQ(cfg.s3, 3);
    ASSERT_EQ(cfg.s4, 4);
    ASSERT_EQ(cfg.h1.min, 10u);
    ASSERT_EQ(cfg.h1.max, 20u);
    ASSERT_EQ(cfg.h2.min, 21u);
    ASSERT_EQ(cfg.h2.max, 21u);
    unset_obf_env();
}

static void test_load_obfuscation_env_invalid(void) {
    awg_config_t cfg;
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));

    ASSERT_EQ(setenv("AWG_JC", "x", 1), 0);
    ASSERT_EQ(load_obfuscation_env(&cfg, &err), -1);
    ASSERT(err && strcmp(err, "AWG_JC: invalid integer") == 0);
    unset_obf_env();

    ASSERT_EQ(setenv("AWG_H1", "20-10", 1), 0);
    err = NULL;
    ASSERT_EQ(load_obfuscation_env(&cfg, &err), -1);
    ASSERT(err && strcmp(err, "AWG_H1: invalid range") == 0);
    unset_obf_env();
}

static void unset_cps_env(void) {
    unsetenv("AWG_I1");
    unsetenv("AWG_I2");
    unsetenv("AWG_I3");
    unsetenv("AWG_I4");
    unsetenv("AWG_I5");
}

static void test_load_cps_env_valid(void) {
    awg_config_t cfg;
    cps_template_t storage[5];
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));
    memset(storage, 0, sizeof(storage));
    unset_cps_env();
    ASSERT_EQ(setenv("AWG_I1", "<r 16>", 1), 0);
    ASSERT_EQ(setenv("AWG_I3", "<b 0xdeadbeef>", 1), 0);
    ASSERT_EQ(load_cps_env(&cfg, storage, &err), 0);
    ASSERT(cfg.cps[0] == &storage[0]);
    ASSERT(cfg.cps[2] == &storage[2]);
    ASSERT(cfg.cps[1] == NULL);
    unset_cps_env();
}

static void test_load_cps_env_invalid(void) {
    awg_config_t cfg;
    cps_template_t storage[5];
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));
    memset(storage, 0, sizeof(storage));
    unset_cps_env();
    ASSERT_EQ(setenv("AWG_I2", "<r -1>", 1), 0);
    ASSERT_EQ(load_cps_env(&cfg, storage, &err), -1);
    ASSERT(err && strcmp(err, "AWG_I2") == 0);
    unset_cps_env();
}

static void test_merge_endpoint_values_env_only(void) {
    char listen[64];
    char remote[64];
    memset(listen, 'x', sizeof(listen));
    memset(remote, 'x', sizeof(remote));
    ASSERT_EQ(merge_endpoint_values("0.0.0.0:51820", "vpn.example.com:51820",
                                    NULL, 0, NULL, 0, listen, sizeof(listen),
                                    remote, sizeof(remote)),
              0);
    ASSERT(strcmp(listen, "0.0.0.0:51820") == 0);
    ASSERT(strcmp(remote, "vpn.example.com:51820") == 0);
}

static void test_merge_endpoint_values_file_overrides_env(void) {
    char listen[64];
    char remote[64];
    ASSERT_EQ(merge_endpoint_values("1.1.1.1:1", "2.2.2.2:2", "0.0.0.0:51820",
                                    1, "host:51820", 1, listen, sizeof(listen),
                                    remote, sizeof(remote)),
              0);
    ASSERT(strcmp(listen, "0.0.0.0:51820") == 0);
    ASSERT(strcmp(remote, "host:51820") == 0);
}

static void test_merge_endpoint_values_empty_and_invalid(void) {
    char listen[8];
    char remote[8];
    ASSERT_EQ(merge_endpoint_values(NULL, NULL, NULL, 0, NULL, 0, listen,
                                    sizeof(listen), remote, sizeof(remote)),
              0);
    ASSERT(strcmp(listen, "") == 0);
    ASSERT(strcmp(remote, "") == 0);
    ASSERT_EQ(merge_endpoint_values(NULL, NULL, NULL, 0, NULL, 0, NULL, 0,
                                    remote, sizeof(remote)),
              -1);
}

static void test_select_dns_value_env_only(void) {
    const char *v = select_dns_value("1.1.1.1,8.8.8.8", NULL, 0);
    ASSERT(v != NULL);
    ASSERT(strcmp(v, "1.1.1.1,8.8.8.8") == 0);
}

static void test_select_dns_value_file_priority(void) {
    const char *v = select_dns_value("1.1.1.1", "9.9.9.9", 1);
    ASSERT(v != NULL);
    ASSERT(strcmp(v, "9.9.9.9") == 0);
}

static void test_select_dns_value_none(void) {
    ASSERT(select_dns_value(NULL, NULL, 0) == NULL);
    ASSERT(select_dns_value("", "", 1) == NULL);
}

static void test_apply_file_obfuscation_overrides(void) {
    awg_config_t cfg;
    awg_file_config_t fc;
    memset(&cfg, 0, sizeof(cfg));
    memset(&fc, 0, sizeof(fc));

    cfg.jc = 1;
    cfg.jmin = 2;
    cfg.jmax = 3;
    cfg.s1 = 4;
    cfg.s2 = 5;
    cfg.s3 = 6;
    cfg.s4 = 7;
    cfg.h1.min = cfg.h1.max = 10;
    cfg.h2.min = cfg.h2.max = 11;
    cfg.h3.min = cfg.h3.max = 12;
    cfg.h4.min = cfg.h4.max = 13;

    fc.have_jc = 1;
    fc.jc = 100;
    fc.have_s2 = 1;
    fc.s2 = 200;
    fc.have_h3 = 1;
    fc.h3.min = 300;
    fc.h3.max = 301;

    apply_file_obfuscation_overrides(&cfg, &fc);

    ASSERT_EQ(cfg.jc, 100);
    ASSERT_EQ(cfg.jmin, 2);
    ASSERT_EQ(cfg.jmax, 3);
    ASSERT_EQ(cfg.s1, 4);
    ASSERT_EQ(cfg.s2, 200);
    ASSERT_EQ(cfg.s3, 6);
    ASSERT_EQ(cfg.s4, 7);
    ASSERT_EQ(cfg.h1.min, 10u);
    ASSERT_EQ(cfg.h3.min, 300u);
    ASSERT_EQ(cfg.h3.max, 301u);
}

static void unset_morph_env(void) {
    unsetenv("AWG_MORPH_KEY");
    unsetenv("AWG_MORPH_KEY_FILE");
}

static void test_load_morph_key_env(void) {
    awg_config_t cfg;
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));
    unset_morph_env();

    ASSERT_EQ(setenv("AWG_MORPH_KEY",
                     "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", 1),
              0);
    ASSERT_EQ(load_morph_key_env(&cfg, &err), 1);
    ASSERT(cfg.morph_enabled);
    for (int i = 0; i < 32; i++)
        ASSERT_EQ(cfg.morph_key[i], 0);

    memset(&cfg, 0, sizeof(cfg));
    ASSERT_EQ(setenv("AWG_MORPH_KEY",
                     "000102030405060708090a0b0c0d0e0f"
                     "101112131415161718191a1b1c1d1e1f",
                     1),
              0);
    ASSERT_EQ(load_morph_key_env(&cfg, &err), 1);
    for (int i = 0; i < 32; i++)
        ASSERT_EQ(cfg.morph_key[i], (uint8_t)i);

    memset(&cfg, 0, sizeof(cfg));
    ASSERT_EQ(setenv("AWG_MORPH_KEY", "invalid", 1), 0);
    ASSERT_EQ(load_morph_key_env(&cfg, &err), -1);
    ASSERT(err != NULL);
    unset_morph_env();
}

static void test_load_morph_key_file_precedence(void) {
    char path[] = "/tmp/morph-keyXXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    const char *file_key = "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=\n";
    ASSERT_EQ(write(fd, file_key, strlen(file_key)), (ssize_t)strlen(file_key));
    ASSERT_EQ(close(fd), 0);

    awg_config_t cfg;
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));
    unset_morph_env();
    ASSERT_EQ(setenv("AWG_MORPH_KEY",
                     "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", 1),
              0);
    ASSERT_EQ(setenv("AWG_MORPH_KEY_FILE", path, 1), 0);
    ASSERT_EQ(load_morph_key_env(&cfg, &err), 1);
    for (int i = 0; i < 32; i++)
        ASSERT_EQ(cfg.morph_key[i], 1);

    unlink(path);
    unset_morph_env();
}

static void test_apply_file_morph_override(void) {
    awg_config_t cfg;
    awg_file_config_t fc;
    memset(&cfg, 0, sizeof(cfg));
    memset(&fc, 0, sizeof(fc));
    memset(cfg.morph_key, 0xaa, sizeof(cfg.morph_key));
    for (int i = 0; i < 32; i++)
        fc.morph_key[i] = (uint8_t)i;
    fc.have_morph_key = 1;

    apply_file_morph_override(&cfg, &fc);
    ASSERT(cfg.morph_enabled);
    for (int i = 0; i < 32; i++)
        ASSERT_EQ(cfg.morph_key[i], (uint8_t)i);
}

static void test_morph_key_config_precedence(void) {
    awg_config_t cfg;
    awg_file_config_t fc;
    const char *err = NULL;
    memset(&cfg, 0, sizeof(cfg));
    memset(&fc, 0, sizeof(fc));
    unset_morph_env();

    ASSERT_EQ(setenv("AWG_MORPH_KEY",
                     "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", 1),
              0);
    ASSERT_EQ(load_morph_key_env(&cfg, &err), 1);
    memset(fc.morph_key, 2, sizeof(fc.morph_key));
    fc.have_morph_key = 1;
    apply_file_morph_override(&cfg, &fc);
    for (int i = 0; i < 32; i++)
        ASSERT_EQ(cfg.morph_key[i], 2);
    unset_morph_env();
}

static void test_morph_obfuscation_env_conflicts(void) {
    static const char *const envs[] = {
        "AWG_JC", "AWG_JMIN", "AWG_JMAX", "AWG_S1", "AWG_S2", "AWG_S3",
        "AWG_S4", "AWG_H1",   "AWG_H2",   "AWG_H3", "AWG_H4",
    };
    for (size_t i = 0; i < sizeof(envs) / sizeof(envs[0]); i++)
        unsetenv(envs[i]);
    for (size_t i = 0; i < sizeof(envs) / sizeof(envs[0]); i++) {
        ASSERT_EQ(setenv(envs[i], "1", 1), 0);
        ASSERT(morph_obfuscation_env_conflict());
        unsetenv(envs[i]);
    }
    ASSERT(!morph_obfuscation_env_conflict());
}

static void test_morph_obfuscation_file_conflicts(void) {
    awg_file_config_t fc;
    memset(&fc, 0, sizeof(fc));
    ASSERT(!morph_obfuscation_file_conflict(&fc));

    int *flags[] = {
        &fc.have_jc, &fc.have_jmin, &fc.have_jmax, &fc.have_s1,
        &fc.have_s2, &fc.have_s3,   &fc.have_s4,   &fc.have_h1,
        &fc.have_h2, &fc.have_h3,   &fc.have_h4,
    };
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        *flags[i] = 1;
        ASSERT(morph_obfuscation_file_conflict(&fc));
        *flags[i] = 0;
    }
}

static void test_load_obfs_profile_env_default_off(void) {
    awg_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    unsetenv("AWG_OBFS_PROFILE");
    ASSERT_EQ(load_obfs_profile_env(&cfg), 0);
    ASSERT_EQ(cfg.obfs_profile, AWG_OBFS_OFF);
}

static void test_load_obfs_profile_env_known_values(void) {
    awg_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    ASSERT_EQ(setenv("AWG_OBFS_PROFILE", "stun_ice", 1), 0);
    ASSERT_EQ(load_obfs_profile_env(&cfg), 0);
    ASSERT_EQ(cfg.obfs_profile, AWG_OBFS_STUN_ICE);

    ASSERT_EQ(setenv("AWG_OBFS_PROFILE", "DTLS_RECORD", 1), 0);
    ASSERT_EQ(load_obfs_profile_env(&cfg), 0);
    ASSERT_EQ(cfg.obfs_profile, AWG_OBFS_DTLS_RECORD);

    ASSERT_EQ(setenv("AWG_OBFS_PROFILE", "game_enet", 1), 0);
    ASSERT_EQ(load_obfs_profile_env(&cfg), 0);
    ASSERT_EQ(cfg.obfs_profile, AWG_OBFS_GAME_ENET);

    ASSERT_EQ(setenv("AWG_OBFS_PROFILE", "DNS_LIKE", 1), 0);
    ASSERT_EQ(load_obfs_profile_env(&cfg), 0);
    ASSERT_EQ(cfg.obfs_profile, AWG_OBFS_DNS_LIKE);
}

static void test_load_obfs_profile_env_unknown_is_off(void) {
    awg_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    ASSERT_EQ(setenv("AWG_OBFS_PROFILE", "unknown_profile", 1), 0);
    ASSERT_EQ(load_obfs_profile_env(&cfg), 0);
    ASSERT_EQ(cfg.obfs_profile, AWG_OBFS_OFF);
}

static void test_apply_file_obfs_profile_override(void) {
    awg_config_t cfg;
    awg_file_config_t fc;
    memset(&cfg, 0, sizeof(cfg));
    memset(&fc, 0, sizeof(fc));
    cfg.obfs_profile = AWG_OBFS_STUN_ICE;
    fc.have_obfs_profile = 1;
    fc.obfs_profile = AWG_OBFS_GAME_KCP;

    apply_file_obfs_profile_override(&cfg, &fc);
    ASSERT_EQ(cfg.obfs_profile, AWG_OBFS_GAME_KCP);
}

int main(void) {
    RUN_TEST(mode_client);
    RUN_TEST(mode_gateway);
    RUN_TEST(mode_server);
    RUN_TEST(mode_case_insensitive);
    RUN_TEST(mode_invalid);
    RUN_TEST(log_level_valid);
    RUN_TEST(log_level_case_insensitive);
    RUN_TEST(log_level_invalid);
    RUN_TEST(hrange_value);
    RUN_TEST(hrange_range);
    RUN_TEST(hrange_invalid);
    RUN_TEST(parse_int_strict_valid);
    RUN_TEST(parse_int_strict_invalid);
    RUN_TEST(parse_src_port_valid);
    RUN_TEST(parse_src_port_invalid);
    RUN_TEST(write_dns_resolv_conf_basic);
    RUN_TEST(write_dns_resolv_conf_spaces);
    RUN_TEST(write_dns_resolv_conf_invalid);
    RUN_TEST(parse_server_peer_list_valid_and_dedup);
    RUN_TEST(parse_server_peer_list_invalid);
    RUN_TEST(load_server_peer_file_valid);
    RUN_TEST(load_operational_env_defaults);
    RUN_TEST(load_operational_env_valid);
    RUN_TEST(load_operational_env_invalid);
    RUN_TEST(compute_remote_silent_timeout);
    RUN_TEST(load_network_perf_env_defaults);
    RUN_TEST(load_network_perf_env_valid);
    RUN_TEST(load_network_perf_env_invalid);
    RUN_TEST(load_obfuscation_env_defaults);
    RUN_TEST(load_obfuscation_env_valid);
    RUN_TEST(load_obfuscation_env_invalid);
    RUN_TEST(load_cps_env_valid);
    RUN_TEST(load_cps_env_invalid);
    RUN_TEST(merge_endpoint_values_env_only);
    RUN_TEST(merge_endpoint_values_file_overrides_env);
    RUN_TEST(merge_endpoint_values_empty_and_invalid);
    RUN_TEST(select_dns_value_env_only);
    RUN_TEST(select_dns_value_file_priority);
    RUN_TEST(select_dns_value_none);
    RUN_TEST(apply_file_obfuscation_overrides);
    RUN_TEST(load_morph_key_env);
    RUN_TEST(load_morph_key_file_precedence);
    RUN_TEST(apply_file_morph_override);
    RUN_TEST(morph_key_config_precedence);
    RUN_TEST(morph_obfuscation_env_conflicts);
    RUN_TEST(morph_obfuscation_file_conflicts);
    RUN_TEST(load_obfs_profile_env_default_off);
    RUN_TEST(load_obfs_profile_env_known_values);
    RUN_TEST(load_obfs_profile_env_unknown_is_off);
    RUN_TEST(apply_file_obfs_profile_override);
    TEST_MAIN_END();
}
