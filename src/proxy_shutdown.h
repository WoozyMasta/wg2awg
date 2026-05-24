#ifndef PROXY_SHUTDOWN_H
#define PROXY_SHUTDOWN_H

#include "proxy.h"
#include <pthread.h>

void proxy_shutdown_sockets(proxy_t *p);
void proxy_join_threads(pthread_t t_c2s, pthread_t t_s2c);
void proxy_cleanup_resources(proxy_t *p);

#endif /* PROXY_SHUTDOWN_H */
