#include "proxy_runtime.h"
#include "log.h"
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>

void proxy_set_thread_affinity(int cpu, const char *name) {
    if (cpu < 0)
        return;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0) {
        char buf[12];
        const char *parts[] = {name, " pinned to cpu", u32_to_str(buf, cpu)};
        log_infon(parts, 3);
    }
}

void proxy_log_socket_buffers(int fd, const awg_config_t *cfg,
                              const char *label) {
    int r = 0, w = 0;
    socklen_t len = sizeof(r);
    getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &r, &len);
    len = sizeof(w);
    getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &w, &len);
    char rb[12], wb[12], reqb[12];
    const char *parts[] = {label,
                           " socket buf: requested=",
                           u32_to_str(reqb, cfg->socket_buf / 1024),
                           "KB, actual read=",
                           u32_to_str(rb, r / 1024),
                           "KB write=",
                           u32_to_str(wb, w / 1024),
                           "KB"};
    log_infon(parts, 8);
}
