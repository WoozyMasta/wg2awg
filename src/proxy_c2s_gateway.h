#ifndef AWG_PROXY_C2S_GATEWAY_H
#define AWG_PROXY_C2S_GATEWAY_H

#include "proxy.h"

void *proxy_c2s_thread_gateway(
    void *arg,
    void (*log_addr_cb)(const char *, const struct sockaddr_storage *));

#endif
