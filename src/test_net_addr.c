#include <stdint.h>
#include <string.h>
#include "test.h"
#include "net_addr.h"

static void test_parse_host_port_ipv4(void) {
    char host[256];
    uint16_t port = 0;
    ASSERT_EQ(
        net_addr_parse_host_port("127.0.0.1:51820", host, sizeof(host), &port),
        0);
    ASSERT(strcmp(host, "127.0.0.1") == 0);
    ASSERT_EQ(port, 51820);
}

static void test_parse_host_port_ipv6_bracketed(void) {
    char host[256];
    uint16_t port = 0;
    ASSERT_EQ(net_addr_parse_host_port("[2001:db8::1]:443", host, sizeof(host),
                                       &port),
              0);
    ASSERT(strcmp(host, "2001:db8::1") == 0);
    ASSERT_EQ(port, 443);
}

static void test_parse_host_port_invalid(void) {
    char host[256];
    uint16_t port = 0;
    ASSERT_EQ(
        net_addr_parse_host_port("2001:db8::1:443", host, sizeof(host), &port),
        -1);
    ASSERT_EQ(net_addr_parse_host_port("host:", host, sizeof(host), &port), -1);
    ASSERT_EQ(net_addr_parse_host_port("host:0", host, sizeof(host), &port),
              -1);
    ASSERT_EQ(net_addr_parse_host_port(NULL, host, sizeof(host), &port), -1);
}

static void test_resolve_host_port_localhost(void) {
    struct sockaddr_storage addr;
    socklen_t len = 0;
    char host[128];
    ASSERT_EQ(net_addr_resolve_host_port("localhost", 53, 0, &addr, &len), 0);
    ASSERT(len > 0);
    ASSERT(net_addr_to_host(&addr, host, sizeof(host)) == 0);
    ASSERT(host[0] != '\0');
    ASSERT_EQ(net_addr_port_host(&addr), 53);
}

int main(void) {
    RUN_TEST(parse_host_port_ipv4);
    RUN_TEST(parse_host_port_ipv6_bracketed);
    RUN_TEST(parse_host_port_invalid);
    RUN_TEST(resolve_host_port_localhost);
    TEST_MAIN_END();
}
