#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "test.h"
#include "config_file.h"
#include "curve25519.h"
#include "base64.h"

static int parse_from_text(const char *text, awg_file_config_t *out) {
    char path[] = "/tmp/awgcfgXXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return -1;

    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        unlink(path);
        return -1;
    }

    size_t n = strlen(text);
    int ok = (fwrite(text, 1, n, f) == n) ? 0 : -1;
    fclose(f);
    if (ok < 0) {
        unlink(path);
        return -1;
    }

    int rc = config_file_parse(path, out);
    unlink(path);
    return rc;
}

static void test_parse_valid_full(void) {
    /* 32-byte all-zero key in base64 */
    const char *k0 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
    /* 32-byte key with first byte = 1, rest = 0 */
    const char *k1 = "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

    const char *cfg_text =
        " [Interface]\n"
        "PrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "ListenPort = 51820\n"
        "DNS = 1.1.1.1,8.8.8.8\n"
        "Jc = 5\n"
        "Jmin = 30\n"
        "Jmax = 500\n"
        "S1 = 20\n"
        "S2 = 21\n"
        "S3 = 22\n"
        "S4 = 23\n"
        "H1 = 100-110\n"
        "H2 = 101\n"
        "H3 = 102\n"
        "H4 = 103\n"
        "I1 = <r 16>\n"
        "I2 = <b 0xDEAD>\n"
        "\n"
        "[Peer]\n"
        "PublicKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "Endpoint = vpn.example.com:443\n"
        "\n"
        "[Peer]\n"
        "PublicKey = AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n";

    awg_file_config_t out;
    ASSERT_EQ(parse_from_text(cfg_text, &out), 0);

    ASSERT(out.have_client_pub);
    ASSERT(out.have_listen);
    ASSERT(out.have_dns);
    ASSERT(out.have_server_pub);
    ASSERT(out.have_endpoint);
    ASSERT_EQ(out.peer_pub_count, 2);

    ASSERT(strcmp(out.listen, "0.0.0.0:51820") == 0);
    ASSERT(strcmp(out.dns, "1.1.1.1,8.8.8.8") == 0);
    ASSERT(strcmp(out.endpoint, "vpn.example.com:443") == 0);

    ASSERT(out.have_jc && out.jc == 5);
    ASSERT(out.have_jmin && out.jmin == 30);
    ASSERT(out.have_jmax && out.jmax == 500);
    ASSERT(out.have_s1 && out.s1 == 20);
    ASSERT(out.have_s2 && out.s2 == 21);
    ASSERT(out.have_s3 && out.s3 == 22);
    ASSERT(out.have_s4 && out.s4 == 23);
    ASSERT(out.have_h1 && out.h1.min == 100 && out.h1.max == 110);
    ASSERT(out.have_h2 && out.h2.min == 101 && out.h2.max == 101);
    ASSERT(out.have_h3 && out.h3.min == 102 && out.h3.max == 102);
    ASSERT(out.have_h4 && out.h4.min == 103 && out.h4.max == 103);
    ASSERT(out.have_cps[0]);
    ASSERT(out.have_cps[1]);
    ASSERT_EQ(out.cps[0].nseg, 1);
    ASSERT_EQ(out.cps[1].nseg, 1);

    {
        uint8_t sk[32], exp_pub[32], dec0[32], dec1[32];
        ASSERT_EQ(base64_decode(k0, (int)strlen(k0), sk, 32), 32);
        curve25519_public_key(exp_pub, sk);
        ASSERT_MEM_EQ(out.client_pub, exp_pub, 32);

        ASSERT_EQ(base64_decode(k0, (int)strlen(k0), dec0, 32), 32);
        ASSERT_EQ(base64_decode(k1, (int)strlen(k1), dec1, 32), 32);
        ASSERT_MEM_EQ(out.server_pub, dec0, 32);
        ASSERT_MEM_EQ(out.peer_pubs[0], dec0, 32);
        ASSERT_MEM_EQ(out.peer_pubs[1], dec1, 32);
    }
}

static void test_case_insensitive_keys(void) {
    const char *cfg_text =
        "[interface]\n"
        "privatekey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "listenport = 12345\n"
        "\n"
        "[peer]\n"
        "publickey = AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "endpoint = 10.0.0.1:51820\n";

    awg_file_config_t out;
    ASSERT_EQ(parse_from_text(cfg_text, &out), 0);
    ASSERT(out.have_client_pub);
    ASSERT(out.have_listen);
    ASSERT(out.have_server_pub);
    ASSERT(out.have_endpoint);
    ASSERT_EQ(out.peer_pub_count, 1);
    ASSERT(strcmp(out.listen, "0.0.0.0:12345") == 0);
    ASSERT(strcmp(out.endpoint, "10.0.0.1:51820") == 0);
}

static void test_invalid_listen_port(void) {
    const char *cfg_text = "[Interface]\n"
                           "ListenPort = 70000\n";
    awg_file_config_t out;
    ASSERT_EQ(parse_from_text(cfg_text, &out), -1);
}

static void test_invalid_private_key_base64(void) {
    const char *cfg_text = "[Interface]\n"
                           "PrivateKey = not_base64\n";
    awg_file_config_t out;
    ASSERT_EQ(parse_from_text(cfg_text, &out), -1);
}

static void test_inline_comments_are_ignored(void) {
    const char *cfg_text =
        "[Interface]\n"
        "PrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA= ; comment\n"
        "ListenPort = 51820 # comment\n"
        "DNS = 9.9.9.9,1.1.1.1 ; trailing\n"
        "Jc = 7 # trailing\n"
        "H1 = 123 ; trailing\n"
        "\n"
        "[Peer]\n"
        "PublicKey = AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA= # first peer "
        "key\n"
        "Endpoint = host.example.com:443 ; endpoint\n";

    awg_file_config_t out;
    ASSERT_EQ(parse_from_text(cfg_text, &out), 0);
    ASSERT(out.have_client_pub);
    ASSERT(out.have_listen);
    ASSERT(out.have_dns);
    ASSERT(out.have_jc);
    ASSERT(out.have_h1);
    ASSERT(out.have_server_pub);
    ASSERT(out.have_endpoint);
    ASSERT_EQ(out.peer_pub_count, 1);

    ASSERT(strcmp(out.listen, "0.0.0.0:51820") == 0);
    ASSERT(strcmp(out.dns, "9.9.9.9,1.1.1.1") == 0);
    ASSERT_EQ(out.jc, 7);
    ASSERT_EQ(out.h1.min, 123u);
    ASSERT_EQ(out.h1.max, 123u);
    ASSERT(strcmp(out.endpoint, "host.example.com:443") == 0);
}

static void test_invalid_hrange(void) {
    const char *cfg_text = "[Interface]\n"
                           "H1 = 200-100\n";
    awg_file_config_t out;
    ASSERT_EQ(parse_from_text(cfg_text, &out), -1);
}

static void test_invalid_cps_template(void) {
    const char *cfg_text = "[Interface]\n"
                           "I1 = <r -1>\n";
    awg_file_config_t out;
    ASSERT_EQ(parse_from_text(cfg_text, &out), -1);
}

static void test_multiple_peers_without_endpoint(void) {
    const char *cfg_text =
        "[Interface]\n"
        "PrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "\n"
        "[Peer]\n"
        "PublicKey = AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "\n"
        "[Peer]\n"
        "PublicKey = AgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n";

    awg_file_config_t out;
    ASSERT_EQ(parse_from_text(cfg_text, &out), 0);

    ASSERT(out.have_client_pub);
    ASSERT(out.have_server_pub);
    ASSERT(!out.have_endpoint);
    ASSERT_EQ(out.peer_pub_count, 2);

    {
        uint8_t p1[32], p2[32];
        ASSERT_EQ(base64_decode("AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
                                44, p1, 32),
                  32);
        ASSERT_EQ(base64_decode("AgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
                                44, p2, 32),
                  32);
        ASSERT_MEM_EQ(out.server_pub, p1, 32);
        ASSERT_MEM_EQ(out.peer_pubs[0], p1, 32);
        ASSERT_MEM_EQ(out.peer_pubs[1], p2, 32);
    }
}

int main(void) {
    fprintf(stderr, "=== config file tests ===\n");
    RUN_TEST(parse_valid_full);
    RUN_TEST(case_insensitive_keys);
    RUN_TEST(invalid_listen_port);
    RUN_TEST(invalid_private_key_base64);
    RUN_TEST(inline_comments_are_ignored);
    RUN_TEST(invalid_hrange);
    RUN_TEST(invalid_cps_template);
    RUN_TEST(multiple_peers_without_endpoint);
    TEST_MAIN_END();
}
