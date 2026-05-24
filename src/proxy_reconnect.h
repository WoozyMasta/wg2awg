#ifndef AWG_PROXY_RECONNECT_H
#define AWG_PROXY_RECONNECT_H

#include "proxy.h"

int proxy_do_reconnect(proxy_t *p, int (*dial_remote_cb)(proxy_t *, int));

#endif
