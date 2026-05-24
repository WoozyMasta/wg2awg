#ifndef AWG_PROXY_STARTUP_H
#define AWG_PROXY_STARTUP_H

#include "proxy.h"

int proxy_startup_network(proxy_t *p, int (*dial_remote_cb)(proxy_t *, int),
                          void (*log_socket_buffers_cb)(int,
                                                        const awg_config_t *,
                                                        const char *));

#endif
