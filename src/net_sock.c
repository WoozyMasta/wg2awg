#include "net_sock.h"
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>

int net_sock_create_udp(int family, int blocking) {
    int flags = SOCK_DGRAM | SOCK_CLOEXEC;
    if (!blocking)
        flags |= SOCK_NONBLOCK;
    return socket(family, flags, 0);
}

void net_sock_set_buffers(int fd, int size) {
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
}

void net_sock_set_busy_poll(int fd, int usec, int budget) {
    if (usec <= 0)
        return;
    setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &usec, sizeof(usec));
#ifdef SO_BUSY_POLL_BUDGET
    if (budget > 0)
        setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL_BUDGET, &budget,
                   sizeof(budget));
#else
    (void)budget;
#endif
}

int net_sock_set_reuseaddr(int fd) {
    int opt = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

int net_sock_bind(int fd, const struct sockaddr_storage *addr,
                  socklen_t addr_len) {
    return bind(fd, (const struct sockaddr *)addr, addr_len);
}

int net_sock_connect(int fd, const struct sockaddr_storage *addr,
                     socklen_t addr_len) {
    return connect(fd, (const struct sockaddr *)addr, addr_len);
}

int net_sock_bind_any_port(int fd, int family, uint16_t port) {
    struct sockaddr_storage local;
    socklen_t local_len;
    memset(&local, 0, sizeof(local));
    if (family == AF_INET6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&local;
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(port);
        local_len = (socklen_t)sizeof(*in6);
    } else {
        struct sockaddr_in *in = (struct sockaddr_in *)&local;
        in->sin_family = AF_INET;
        in->sin_port = htons(port);
        in->sin_addr.s_addr = htonl(INADDR_ANY);
        local_len = (socklen_t)sizeof(*in);
    }

    if (net_sock_set_reuseaddr(fd) < 0)
        return -1;
    if (net_sock_bind(fd, &local, local_len) < 0)
        return -1;
    return 0;
}
