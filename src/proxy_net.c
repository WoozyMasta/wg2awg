#include "proxy_net.h"
#include "log.h"
#include "net_addr.h"
#include "net_sock.h"
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>

static int monotonic_now_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    if (ts.tv_sec <= 0)
        return 0;
    if (ts.tv_sec > INT_MAX)
        return INT_MAX;
    return (int)ts.tv_sec;
}

static void proxy_reset_dns_resolve_failure_window(proxy_t *p) {
    p->dns_resolve_fail_started_at = 0;
}

static void proxy_note_dns_resolve_failure(proxy_t *p, const char *host,
                                           int gai_err) {
    const char *msg = gai_strerror(gai_err);
    if (!msg)
        msg = "unknown resolver error";
    const char *parts[] = {"resolve ", host, ": ", msg};
    log_msgn("ERROR: ", parts, 4);

    if (!p)
        return;
    if (p->cfg->dns_resolve_failure_timeout <= 0)
        return;

    int now = monotonic_now_sec();
    if (p->dns_resolve_fail_started_at <= 0)
        p->dns_resolve_fail_started_at = now;

    int elapsed = now - p->dns_resolve_fail_started_at;
    if (elapsed < p->cfg->dns_resolve_failure_timeout)
        return;

    char eb[12], tb[12];
    const char *timeout_parts[] = {
        "DNS resolve failure timeout exceeded: elapsed=",
        u32_to_str(eb, (unsigned)elapsed), "s limit=",
        u32_to_str(tb, (unsigned)p->cfg->dns_resolve_failure_timeout),
        "s, exiting"};
    log_msgn("ERROR: ", timeout_parts, 5);
    _exit(1);
}

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
    int gai_err = 0;
    if (net_addr_resolve_host_port(host, port, 0, addr, addr_len, &gai_err) <
        0) {
        proxy_note_dns_resolve_failure((proxy_t *)0, host, gai_err);
        return -1;
    }
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
    int gai_err = 0;
    log_info2("resolving ", p->remote_host);
    if (net_addr_resolve_host_port(p->remote_host, p->remote_port, 0,
                                   &p->remote_addr, &p->remote_addr_len,
                                   &gai_err) < 0) {
        proxy_note_dns_resolve_failure(p, p->remote_host, gai_err);
        return -1;
    }
    proxy_reset_dns_resolve_failure_window(p);
    if (g_log_level >= LOG_INFO) {
        char hostbuf[INET6_ADDRSTRLEN];
        if (net_addr_to_host(&p->remote_addr, hostbuf, sizeof(hostbuf)) == 0) {
            const char *parts[] = {"resolved ", p->remote_host, " -> ",
                                   hostbuf};
            log_infon(parts, 4);
        }
    }

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
