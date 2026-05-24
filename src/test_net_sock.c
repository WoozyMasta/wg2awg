#include <unistd.h>
#include <netinet/in.h>
#include "test.h"
#include "net_sock.h"

static void test_create_udp_socket(void) {
    int fd = net_sock_create_udp(AF_INET, 1);
    ASSERT(fd >= 0);
    close(fd);
}

static void test_set_buffers_and_busy_poll(void) {
    int fd = net_sock_create_udp(AF_INET, 1);
    ASSERT(fd >= 0);
    net_sock_set_buffers(fd, 1 << 20);
    net_sock_set_busy_poll(fd, 0, 32);
    close(fd);
}

static void test_bind_any_port(void) {
    int fd = net_sock_create_udp(AF_INET, 1);
    ASSERT(fd >= 0);
    ASSERT_EQ(net_sock_bind_any_port(fd, AF_INET, 0), 0);
    close(fd);
}

int main(void) {
    RUN_TEST(create_udp_socket);
    RUN_TEST(set_buffers_and_busy_poll);
    RUN_TEST(bind_any_port);
    TEST_MAIN_END();
}
