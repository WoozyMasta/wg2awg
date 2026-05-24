#ifndef PROXY_RUNTIME_H
#define PROXY_RUNTIME_H

#include "proxy.h"

void proxy_set_thread_affinity(int cpu, const char *name);
void proxy_log_socket_buffers(int fd, const awg_config_t *cfg,
                              const char *label);

#endif /* PROXY_RUNTIME_H */
