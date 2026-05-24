#ifndef AWG_NET_SOCK_H
#define AWG_NET_SOCK_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

int net_sock_create_udp(int family, int blocking);
void net_sock_set_buffers(int fd, int size);
void net_sock_set_busy_poll(int fd, int usec, int budget);
int net_sock_set_reuseaddr(int fd);
int net_sock_bind(int fd, const struct sockaddr_storage *addr,
                  socklen_t addr_len);
int net_sock_connect(int fd, const struct sockaddr_storage *addr,
                     socklen_t addr_len);
int net_sock_bind_any_port(int fd, int family, uint16_t port);

#endif
