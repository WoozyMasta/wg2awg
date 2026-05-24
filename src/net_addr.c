#include "net_addr.h"
#include <string.h>
#include <stdio.h>
#include <netdb.h>

socklen_t net_addr_socklen(const struct sockaddr_storage *addr) {
    if (addr->ss_family == AF_INET)
        return (socklen_t)sizeof(struct sockaddr_in);
    if (addr->ss_family == AF_INET6)
        return (socklen_t)sizeof(struct sockaddr_in6);
    return (socklen_t)sizeof(*addr);
}

uint16_t net_addr_port_host(const struct sockaddr_storage *addr) {
    if (addr->ss_family == AF_INET)
        return ntohs(((const struct sockaddr_in *)addr)->sin_port);
    if (addr->ss_family == AF_INET6)
        return ntohs(((const struct sockaddr_in6 *)addr)->sin6_port);
    return 0;
}

int net_addr_to_host(const struct sockaddr_storage *addr, char *host,
                     size_t host_len) {
    int r = getnameinfo((const struct sockaddr *)addr, net_addr_socklen(addr),
                        host, (socklen_t)host_len, NULL, 0, NI_NUMERICHOST);
    if (r != 0 || !host[0])
        return -1;
    return 0;
}

int net_addr_resolve_host_port(const char *host, uint16_t port, int passive,
                               struct sockaddr_storage *addr,
                               socklen_t *addr_len) {
    char portstr[6];
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *it = NULL;
    int gai_err;

    snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (passive)
        hints.ai_flags = AI_PASSIVE;

    gai_err =
        getaddrinfo((host && host[0]) ? host : NULL, portstr, &hints, &res);
    if (gai_err != 0)
        return -1;

    for (it = res; it; it = it->ai_next) {
        if (it->ai_family != AF_INET && it->ai_family != AF_INET6)
            continue;
        if ((size_t)it->ai_addrlen > sizeof(*addr))
            continue;
        memset(addr, 0, sizeof(*addr));
        memcpy(addr, it->ai_addr, (size_t)it->ai_addrlen);
        *addr_len = it->ai_addrlen;
        freeaddrinfo(res);
        return 0;
    }

    freeaddrinfo(res);
    return -1;
}

int net_addr_parse_host_port(const char *s, char *host, int hostmax,
                             uint16_t *port) {
    const char *port_str;
    unsigned int pv = 0;

    if (!s || !s[0])
        return -1;

    if (s[0] == '[') {
        const char *rb = strchr(s + 1, ']');
        if (!rb || rb[1] != ':')
            return -1;
        int hlen = (int)(rb - (s + 1));
        if (hlen <= 0 || hlen >= hostmax)
            return -1;
        memcpy(host, s + 1, (size_t)hlen);
        host[hlen] = '\0';
        port_str = rb + 2;
    } else {
        const char *colon = strrchr(s, ':');
        if (!colon)
            return -1;
        int hlen = (int)(colon - s);
        if (hlen < 0 || hlen >= hostmax)
            return -1;
        memcpy(host, s, (size_t)hlen);
        host[hlen] = '\0';
        if (strchr(host, ':'))
            return -1;
        port_str = colon + 1;
    }

    if (!port_str[0])
        return -1;
    for (const char *p = port_str; *p; p++) {
        if (*p < '0' || *p > '9')
            return -1;
        pv = pv * 10u + (unsigned int)(*p - '0');
        if (pv > 65535u)
            return -1;
    }
    if (pv == 0)
        return -1;
    *port = (uint16_t)pv;
    return 0;
}
