#ifndef PROXY_CONTROL_H
#define PROXY_CONTROL_H

#include "proxy.h"

int proxy_control_init(proxy_t *p);
int proxy_control_loop(proxy_t *p, int timeout_secs, int silent_secs);

#endif /* PROXY_CONTROL_H */
