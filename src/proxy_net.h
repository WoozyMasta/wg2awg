#ifndef PROXY_NET_H
#define PROXY_NET_H

#include "proxy.h"

void proxy_log_addr(const char *prefix, const struct sockaddr_storage *addr);
int proxy_resolve_addr(const char *host, uint16_t port,
                       struct sockaddr_storage *addr, socklen_t *addr_len);
int proxy_dial_remote(proxy_t *p, int blocking);

#endif /* PROXY_NET_H */
