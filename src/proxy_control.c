#include "proxy_control.h"
#include "morph.h"
#include "log.h"
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

static void drain_signal_fd(int fd) {
    struct signalfd_siginfo si;
    while (1) {
        ssize_t n = read(fd, &si, sizeof(si));
        if (n == (ssize_t)sizeof(si))
            continue;
        if (n < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return;
        return;
    }
}

static void drain_timer_fd(int fd) {
    uint64_t expirations;
    while (1) {
        ssize_t n = read(fd, &expirations, sizeof(expirations));
        if (n == (ssize_t)sizeof(expirations))
            continue;
        if (n < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return;
        return;
    }
}

int proxy_control_init(proxy_t *p) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    p->signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (p->signal_fd < 0) {
        log_error("signalfd failed");
        return -1;
    }

    p->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (p->timer_fd < 0) {
        log_error("timerfd failed");
        close(p->signal_fd);
        p->signal_fd = -1;
        return -1;
    }

    struct itimerspec ts = {
        .it_interval = {.tv_sec = 5},
        .it_value = {.tv_sec = 5},
    };
    timerfd_settime(p->timer_fd, 0, &ts, NULL);
    return 0;
}

/* Treat the client as "actively trying" until it has been quiet
 * for this many timer ticks (~60s).
 * WireGuard handshake retries are jittery, so a few empty
 * ticks must not reset the one-sided-silence accounting. */
#define CLIENT_ACTIVE_GRACE_CHECKS 12

/* Trigger a recovery reconnect of the remote socket.
 * In auto src-port mode with drift enabled,
 * also request a fresh source port so the new dial bypasses
 * a stale NAT/conntrack mapping instead of reusing the dead 5-tuple. */
static void trigger_recovery_reconnect(proxy_t *p) {
    int rfd = atomic_load_explicit(&p->remote_fd, memory_order_acquire);
    if (rfd < 0)
        return;
    if (p->cfg->src_port_drift && p->auto_src_port)
        atomic_store_explicit(&p->reconnect_drift, 1, memory_order_relaxed);
    atomic_store_explicit(&p->reconnect_needed, 1, memory_order_relaxed);
    shutdown(rfd, SHUT_RDWR);
}

int proxy_control_loop(proxy_t *p, int timeout_secs, int silent_secs,
                       int silent_exit_secs) {
    int checks_needed = timeout_secs / 5;
    if (checks_needed < 1)
        checks_needed = 1;
    int silent_checks_needed = silent_secs / 5;
    if (silent_checks_needed < 1)
        silent_checks_needed = 1;
    int exit_checks_needed = silent_exit_secs / 5;
    if (silent_exit_secs > 0 && exit_checks_needed < 1)
        exit_checks_needed = 1;
    /* Warn at half the reconnect timeout (~keepalive*2 by default), scaling
     * with the keepalive-derived silent timeout instead of a fixed interval. */
    int warn_checks = silent_checks_needed / 2;
    if (warn_checks < 1)
        warn_checks = 1;

    int inactive_count = 0;
    int remote_silent_count = 0;
    int remote_silent_total = 0;
    int silence_warned = 0;
    int client_idle_ticks = 0;

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        log_error("epoll_create failed");
        atomic_store_explicit(&p->stopped, 1, memory_order_relaxed);
        return -1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = p->signal_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, p->signal_fd, &ev);
    ev.data.fd = p->timer_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, p->timer_fd, &ev);

    struct epoll_event events[2];

    while (!atomic_load_explicit(&p->stopped, memory_order_relaxed)) {
        int nev = epoll_wait(epfd, events, 2, 1000);
        if (nev < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        for (int i = 0; i < nev; i++) {
            int fd = events[i].data.fd;

            if (fd == p->signal_fd) {
                drain_signal_fd(p->signal_fd);
                log_info("shutting down");
                atomic_store_explicit(&p->stopped, 1, memory_order_relaxed);
                break;
            }

            if (fd == p->timer_fd) {
                drain_timer_fd(p->timer_fd);
                if (p->cfg->morph_enabled)
                    morph_update_if_needed(&p->morph, p->cfg,
                                           p->cfg->morph_key);
                int had_activity = atomic_exchange_explicit(
                    &p->last_active, 0, memory_order_relaxed);
                int had_remote_rx = atomic_exchange_explicit(
                    &p->last_remote_rx, 0, memory_order_relaxed);

                if (had_remote_rx) {
                    /* Real traffic from remote: healthy, reset all accounting
                     * and drop back to the preferred source port. */
                    inactive_count = 0;
                    remote_silent_count = 0;
                    remote_silent_total = 0;
                    silence_warned = 0;
                    client_idle_ticks = 0;
                    atomic_store_explicit(&p->reconnect_drift, 0,
                                          memory_order_relaxed);
                    continue;
                }

                /* No remote traffic this tick. */
                if (had_activity)
                    client_idle_ticks = 0;
                else
                    client_idle_ticks++;

                if (client_idle_ticks <= CLIENT_ACTIVE_GRACE_CHECKS) {
                    /* Client is (recently) sending but remote stays silent:
                     * broken path. Accumulate until recovery, not per-tick, so
                     * jittery client gaps do not reset the counters. */
                    inactive_count = 0;
                    remote_silent_count++;
                    remote_silent_total++;

                    if (!silence_warned && remote_silent_total >= warn_checks) {
                        if (g_log_level >= LOG_INFO) {
                            const char *parts[] = {
                                "remote silent while client active, "
                                "will reconnect"};
                            log_msgn("WARN: ", parts, 1);
                        }
                        silence_warned = 1;
                    }
                    if (silent_exit_secs > 0 &&
                        remote_silent_total >= exit_checks_needed) {
                        char nb[12];
                        const char *parts[] = {
                            "remote silent for ",
                            u32_to_str(nb, (unsigned)remote_silent_total * 5),
                            "s while client active, exiting"};
                        if (g_log_level >= LOG_ERROR)
                            log_msgn("ERROR: ", parts, 3);
                        _exit(1);
                    }
                    if (remote_silent_count >= silent_checks_needed) {
                        log_info("remote silent, triggering reconnect");
                        trigger_recovery_reconnect(p);
                        remote_silent_count = 0;
                    }
                } else {
                    /* Genuinely idle: client quiet too, not a broken-path
                     * signal. Only the full-inactivity timeout applies. */
                    remote_silent_count = 0;
                    remote_silent_total = 0;
                    silence_warned = 0;
                    inactive_count++;
                    if (inactive_count >= checks_needed) {
                        log_info("remote timeout, triggering reconnect");
                        trigger_recovery_reconnect(p);
                        inactive_count = 0;
                    }
                }
            }
        }
    }

    close(epfd);
    return 0;
}
