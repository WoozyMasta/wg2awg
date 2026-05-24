#ifndef AWG_NET_ADDR_H
#define AWG_NET_ADDR_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

socklen_t net_addr_socklen(const struct sockaddr_storage *addr);
uint16_t net_addr_port_host(const struct sockaddr_storage *addr);
int net_addr_to_host(const struct sockaddr_storage *addr, char *host,
                     size_t host_len);

int net_addr_parse_host_port(const char *s, char *host, int hostmax,
                             uint16_t *port);
int net_addr_resolve_host_port(const char *host, uint16_t port, int passive,
                               struct sockaddr_storage *addr,
                               socklen_t *addr_len);

#endif
