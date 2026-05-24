#ifndef AWG_PROXY_IO_BATCH_H
#define AWG_PROXY_IO_BATCH_H

#include "proxy.h"

int proxy_io_enable_gro(int fd);
void proxy_io_init_gro_state(proxy_t *p);
int proxy_io_recv_gro(proxy_t *p, int fd, int *seg_size);
void proxy_io_send_batch_gso(proxy_t *p, int fd, struct mmsghdr *msgs,
                             struct iovec *iovecs, int nsend,
                             const struct sockaddr_storage *addr,
                             socklen_t addr_len);

#endif
