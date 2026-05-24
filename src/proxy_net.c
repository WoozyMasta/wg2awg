#include "proxy_net.h"
#include "log.h"
#include "net_addr.h"
#include "net_sock.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>

void proxy_log_addr(const char *prefix, const struct sockaddr_storage *addr) {
    char host[INET6_ADDRSTRLEN] = {0};
    char portb[12];
    if (net_addr_to_host(addr, host, sizeof(host)) < 0)
        return;
    const char *parts[] = {prefix, host, ":",
                           u32_to_str(portb, net_addr_port_host(addr))};
    log_infon(parts, 4);
}

int proxy_resolve_addr(const char *host, uint16_t port,
                       struct sockaddr_storage *addr, socklen_t *addr_len) {
    log_info2("resolving ", host);
    if (net_addr_resolve_host_port(host, port, 0, addr, addr_len) < 0)
        return -1;
    if (g_log_level >= LOG_INFO) {
        char hostbuf[INET6_ADDRSTRLEN];
        if (net_addr_to_host(addr, hostbuf, sizeof(hostbuf)) == 0) {
            const char *parts[] = {"resolved ", host, " -> ", hostbuf};
            log_infon(parts, 4);
        }
    }
    return 0;
}

int proxy_dial_remote(proxy_t *p, int blocking) {
    if (proxy_resolve_addr(p->remote_host, p->remote_port, &p->remote_addr,
                           &p->remote_addr_len) < 0)
        return -1;

    int fd = net_sock_create_udp(p->remote_addr.ss_family, blocking);
    if (fd < 0)
        return -1;

    if (p->local_port > 0) {
        if (net_sock_bind_any_port(fd, p->remote_addr.ss_family,
                                   (uint16_t)p->local_port) < 0) {
            log_error2("bind failed: ", strerror(errno));
            close(fd);
            return -1;
        }
    }

    if (net_sock_connect(fd, &p->remote_addr, p->remote_addr_len) < 0) {
        log_error2("connect failed: ", strerror(errno));
        close(fd);
        return -1;
    }

    if (g_log_level >= LOG_INFO)
        proxy_log_addr("connected to ", &p->remote_addr);

    net_sock_set_buffers(fd, p->cfg->socket_buf);
    net_sock_set_busy_poll(fd, p->cfg->busy_poll, BATCH_SIZE);
    return fd;
}
