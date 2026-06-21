#ifndef AWG_CONFIG_RUNTIME_H
#define AWG_CONFIG_RUNTIME_H

#include "transform.h"
#include "log.h"
#include "config_file.h"

/* Parse AWG_MODE string to internal enum.
 * Accepted values: client, gateway, server.
 * Returns 0 on success, -1 on invalid input. */
int parse_awg_mode(const char *s, int *mode_out);

/* Parse log level by first letter compatibility:
 * n/e/i/d -> none/error/info/debug.
 * Returns 0 on success, -1 on invalid input. */
int parse_log_level(const char *s, int *level_out);

/* Parse "VALUE" or "MIN-MAX" into hrange_t.
 * Returns 0 on success, -1 on invalid input. */
int parse_hrange_str(const char *s, hrange_t *r);

/* Parse signed int with full-string validation and range check.
 * Returns 0 on success, -1 on invalid input or overflow. */
int parse_int_strict(const char *s, int *out);

/* Parse AWG_SRC_PORT value.
 * Accepts "auto" -> out=0, or integer string.
 * Returns 0 on success, -1 on invalid input. */
int parse_src_port(const char *s, int *src_port_out);

/* Write DNS servers into resolv.conf-like file.
 * dns_str is comma/space separated list of resolver IPs.
 * Returns 0 on success, -1 on I/O error or invalid args. */
int write_dns_resolv_conf(const char *path, const char *dns_str);

/* Parse comma/space separated base64 public keys and append unique values to
 * cfg->server_peer_pubs. Returns 0 on success, -1 on decode/limit error. */
int parse_server_peer_list(awg_config_t *cfg, const char *value);

/* Load and parse base64 public keys list from text file.
 * Returns 0 on success, -1 on I/O/parse error. */
int load_server_peer_file(awg_config_t *cfg, const char *path);

/* Append raw 32-byte public key to server peer list if unique.
 * Returns 0 on success, -1 if list is full or args are invalid. */
int add_server_peer_pub_unique(awg_config_t *cfg, const uint8_t pub[32]);

/* Load operational parameters from environment with defaults.
 * Sets
 * timeout/remote_silent_timeout/remote_silent_exit_timeout/connect_retries,
 * dns_resolve_failure_timeout/socket_buf.
 * Returns 0 on success, -1 on invalid values and sets *err_msg. */
int load_operational_env(awg_config_t *cfg, const char **err_msg);

/* Resolve the effective one-sided-silence reconnect timeout (seconds).
 * An explicit value (explicit_secs > 0, e.g. from AWG_REMOTE_SILENT_TIMEOUT)
 * is used as-is; otherwise it is derived from the peer PersistentKeepalive as
 * keepalive*4 (fallback 15 when absent), with a lower bound of 30 and, when the
 * exit guard is enabled (exit_secs > 0), an upper bound of exit_secs/2 so
 * reconnect is attempted before the exit guard fires. */
int compute_remote_silent_timeout(int explicit_secs, int have_keepalive,
                                  int keepalive, int exit_secs);

/* Load network/performance parameters from environment.
 * Sets src_port/cpu_c2s/cpu_s2c/busy_poll/no_gro.
 * Returns 0 on success, -1 on invalid values and sets *err_msg. */
int load_network_perf_env(awg_config_t *cfg, const char **err_msg);

/* Load AWG obfuscation parameters from environment with defaults.
 * Sets jc/jmin/jmax/s1/s2/s3/s4 and h1..h4.
 * Returns 0 on success, -1 on invalid values and sets *err_msg. */
int load_obfuscation_env(awg_config_t *cfg, const char **err_msg);

/* Load obfs profile from AWG_OBFS_PROFILE.
 * Unknown/empty values are treated as "off".
 * Returns 0 on success. */
int load_obfs_profile_env(awg_config_t *cfg);

/* Apply wg2awg ObfsProfile from config file with priority over env. */
void apply_file_obfs_profile_override(awg_config_t *cfg,
                                      const awg_file_config_t *file_cfg);

/* Load CPS templates AWG_I1..AWG_I5 from environment into provided storage.
 * On success assigns cfg->cps[i] for parsed templates.
 * Returns 0 on success, -1 on parse error and sets *err_msg. */
int load_cps_env(awg_config_t *cfg, cps_template_t storage[5],
                 const char **err_msg);

/* Merge listen/remote strings with precedence: config file values override env.
 * Empty input means "not set". Output buffers are always NUL-terminated.
 * Returns 0 on success, -1 on invalid args. */
int merge_endpoint_values(const char *env_listen, const char *env_remote,
                          const char *file_listen, int file_have_listen,
                          const char *file_remote, int file_have_remote,
                          char *listen_out, size_t listen_out_sz,
                          char *remote_out, size_t remote_out_sz);

/* Choose DNS value with precedence: config file value overrides env value.
 * Returns pointer to selected non-empty string or NULL if none is set. */
const char *select_dns_value(const char *env_dns, const char *file_dns,
                             int file_have_dns);

/* Apply obfuscation-related fields from parsed config file onto runtime cfg.
 * Only fields marked by have_* flags are copied. */
void apply_file_obfuscation_overrides(awg_config_t *cfg,
                                      const awg_file_config_t *file_cfg);

/* Load AWG_MORPH_KEY or AWG_MORPH_KEY_FILE from environment.
 * Sets cfg->morph_enabled and cfg->morph_key on success.
 * Returns 0 if no morph key env var is set, 1 if key was loaded,
 * -1 on parse/IO error (sets *err_msg). */
int load_morph_key_env(awg_config_t *cfg, const char **err_msg);

/* Apply MorphKey from config file if have_morph_key is set. */
void apply_file_morph_override(awg_config_t *cfg,
                               const awg_file_config_t *file_cfg);

/* Return non-zero when Morph Mode conflicts with explicit H/S/J settings. */
int morph_obfuscation_env_conflict(void);
int morph_obfuscation_file_conflict(const awg_file_config_t *file_cfg);

#endif
