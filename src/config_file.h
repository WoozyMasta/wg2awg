#ifndef AWG_CONFIG_FILE_H
#define AWG_CONFIG_FILE_H

#include "transform.h"
#include <stdint.h>

/*
 * Parsed representation of an AmneziaWG/WireGuard config file.
 * Fields are populated only when present in the file (have_* flags).
 */
typedef struct {
    /* [Interface] AWG obfuscation params */
    int have_jc;
    int jc;
    int have_jmin;
    int jmin;
    int have_jmax;
    int jmax;
    int have_s1;
    int s1;
    int have_s2;
    int s2;
    int have_s3;
    int s3;
    int have_s4;
    int s4;
    int have_h1;
    hrange_t h1;
    int have_h2;
    hrange_t h2;
    int have_h3;
    hrange_t h3;
    int have_h4;
    hrange_t h4;
    int have_cps[5];
    cps_template_t cps[5];

    /* [Interface] PrivateKey -> derived Curve25519 public key */
    int have_client_pub;
    uint8_t client_pub[32];

    /* [Interface] ListenPort -> "0.0.0.0:PORT" */
    int have_listen;
    char listen[64];

    /* [Interface] DNS (raw comma-separated string) */
    int have_dns;
    char dns[256];

    /* First [Peer] PublicKey -> server_pub in client/gateway mode */
    int have_server_pub;
    uint8_t server_pub[32];

    /* First [Peer] Endpoint -> remote address */
    int have_endpoint;
    char endpoint[256];

    /* First [Peer] PersistentKeepalive (seconds) -> remote-silent default */
    int have_keepalive;
    int keepalive;

    /*
     * All [Peer].PublicKey entries (including the first).
     * Used in server mode where each peer is a WG client.
     */
    int peer_pub_count;
    uint8_t peer_pubs[AWG_MAX_SERVER_PEERS][32];

    /* Morph Mode: MorphKey = <base64> in [Interface] */
    int have_morph_key;
    uint8_t morph_key[32];

    /* wg2awg outer obfuscation extension. */
    int have_obfs_profile;
    awg_obfs_profile_t obfs_profile;
} awg_file_config_t;

/*
 * Parse an AmneziaWG / WireGuard INI-style config file.
 * Returns 0 on success, -1 on error (error is logged).
 */
int config_file_parse(const char *path, awg_file_config_t *out);

#endif
