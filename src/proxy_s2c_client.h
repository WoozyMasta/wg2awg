#ifndef AWG_PROXY_S2C_CLIENT_H
#define AWG_PROXY_S2C_CLIENT_H

#include "proxy.h"

int proxy_s2c_process_client(proxy_t *p, uint8_t *pkt, int n,
                             struct iovec *send_iovecs,
                             struct sockaddr_storage *send_addrs,
                             socklen_t *send_addrlens, int *nsend);

#endif
