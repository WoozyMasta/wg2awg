#include <stdint.h>
#include "test.h"
#include "session_table.h"
#include <string.h>
#include <arpa/inet.h>

static void mk_addr(struct sockaddr_storage *out, uint32_t host_be,
                    uint16_t port_host) {
    memset(out, 0, sizeof(*out));
    struct sockaddr_in *a = (struct sockaddr_in *)out;
    a->sin_family = AF_INET;
    a->sin_addr.s_addr = host_be;
    a->sin_port = htons(port_host);
}

static void test_put_lookup_roundtrip(void) {
    session_entry_t sessions[SESSION_TABLE_SIZE];
    memset(sessions, 0, sizeof(sessions));

    struct sockaddr_storage in_addr;
    mk_addr(&in_addr, htonl(0x0A000001u), 51820);
    session_put(sessions, 42u, &in_addr, sizeof(struct sockaddr_in));

    session_entry_t *entry = NULL;
    struct sockaddr_storage got_addr;
    socklen_t got_len = 0;
    ASSERT(session_lookup(sessions, 42u, &entry, &got_addr, &got_len) == 1);
    ASSERT(entry != NULL);
    ASSERT(got_len == sizeof(struct sockaddr_in));
    ASSERT(session_addr_equal(&in_addr, sizeof(struct sockaddr_in), &got_addr,
                              got_len) == 1);
}

static void test_find_sole_client(void) {
    session_entry_t sessions[SESSION_TABLE_SIZE];
    memset(sessions, 0, sizeof(sessions));

    struct sockaddr_storage a1;
    struct sockaddr_storage a2;
    mk_addr(&a1, htonl(0x0A000001u), 10001);
    mk_addr(&a2, htonl(0x0A000001u), 10001);
    session_put(sessions, 10u, &a1, sizeof(struct sockaddr_in));
    session_put(sessions, 20u, &a2, sizeof(struct sockaddr_in));

    struct sockaddr_storage sole;
    socklen_t sole_len = 0;
    ASSERT(session_find_sole_client(sessions, &sole, &sole_len) == 1);
    ASSERT(sole_len == sizeof(struct sockaddr_in));
    ASSERT(session_addr_equal(&a1, sizeof(struct sockaddr_in), &sole,
                              sole_len) == 1);

    mk_addr(&a2, htonl(0x0A000002u), 10002);
    session_put(sessions, 30u, &a2, sizeof(struct sockaddr_in));
    ASSERT(session_find_sole_client(sessions, &sole, &sole_len) == 0);
}

int main(void) {
    fprintf(stderr, "=== session_table tests ===\n");
    RUN_TEST(put_lookup_roundtrip);
    RUN_TEST(find_sole_client);
    TEST_MAIN_END();
}
