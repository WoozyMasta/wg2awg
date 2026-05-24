#ifndef AWG_PROXY_C2S_CLIENT_H
#define AWG_PROXY_C2S_CLIENT_H

#include "proxy.h"

void *proxy_c2s_thread_client(
    void *arg, uint32_t (*pick_h4_cb)(proxy_t *),
    void (*log_addr_cb)(const char *, const struct sockaddr_storage *));

#endif
