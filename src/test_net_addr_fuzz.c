#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "net_addr.h"

static uint32_t rng_state = 0xA5A5C3E7u;

static uint32_t rng32(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static char rand_alnum(void) {
    static const char *a = "abcdefghijklmnopqrstuvwxyz0123456789";
    return a[rng32() % 36];
}

static void gen_host(char *buf, size_t n) {
    size_t len = 1 + (rng32() % 24);
    if (len >= n)
        len = n - 1;
    for (size_t i = 0; i < len; i++)
        buf[i] = rand_alnum();
    buf[len] = '\0';
}

static void test_parse_host_port_roundtrip_random(void) {
    for (int i = 0; i < 2000; i++) {
        char host[64];
        char spec[96];
        char out_host[64];
        uint16_t out_port = 0;
        uint16_t port = (uint16_t)(1 + (rng32() % 65535));
        gen_host(host, sizeof(host));
        snprintf(spec, sizeof(spec), "%s:%u", host, (unsigned)port);
        ASSERT_EQ(net_addr_parse_host_port(spec, out_host, sizeof(out_host),
                                           &out_port),
                  0);
        ASSERT(strcmp(host, out_host) == 0);
        ASSERT_EQ(out_port, port);
    }
}

static void test_parse_host_port_invalid_random(void) {
    char host[64];
    uint16_t port = 0;

    ASSERT_EQ(net_addr_parse_host_port(NULL, host, sizeof(host), &port), -1);
    ASSERT_EQ(net_addr_parse_host_port("host:", host, sizeof(host), &port), -1);
    ASSERT_EQ(net_addr_parse_host_port("host:0", host, sizeof(host), &port),
              -1);

    for (int i = 0; i < 1200; i++) {
        char s[64];
        size_t len = 1 + (rng32() % 40);
        if (len >= sizeof(s))
            len = sizeof(s) - 1;
        for (size_t j = 0; j < len; j++) {
            char c = rand_alnum();
            if (c == ':')
                c = 'x';
            s[j] = c;
        }
        s[len] = '\0';
        ASSERT(strchr(s, ':') == NULL);
        ASSERT_EQ(net_addr_parse_host_port(s, host, sizeof(host), &port), -1);
    }
}

int main(void) {
    fprintf(stderr, "=== net_addr fuzz-style tests ===\n");
    RUN_TEST(parse_host_port_roundtrip_random);
    RUN_TEST(parse_host_port_invalid_random);
    TEST_MAIN_END();
}
