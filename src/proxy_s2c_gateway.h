#ifndef AWG_PROXY_S2C_GATEWAY_H
#define AWG_PROXY_S2C_GATEWAY_H

#include "proxy.h"

int proxy_s2c_process_gateway(proxy_t *p, uint8_t *base, uint8_t *pkt, int n,
                              int prefix, struct iovec *send_iovecs,
                              struct sockaddr_storage *send_addrs,
                              socklen_t *send_addrlens, int *nsend,
                              uint32_t (*pick_h4_cb)(proxy_t *));

#endif
