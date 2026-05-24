#include "proxy_control.h"
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

int proxy_control_loop(proxy_t *p, int timeout_secs, int silent_secs) {
    int checks_needed = timeout_secs / 5;
    if (checks_needed < 1)
        checks_needed = 1;
    int silent_checks_needed = silent_secs / 5;
    if (silent_checks_needed < 1)
        silent_checks_needed = 1;

    int inactive_count = 0;
    int remote_silent_count = 0;

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
                int had_activity = atomic_exchange_explicit(
                    &p->last_active, 0, memory_order_relaxed);
                int had_remote_rx = atomic_exchange_explicit(
                    &p->last_remote_rx, 0, memory_order_relaxed);
                if (had_activity) {
                    inactive_count = 0;
                    if (!had_remote_rx) {
                        remote_silent_count++;
                        if (remote_silent_count >= silent_checks_needed) {
                            log_info("remote silent (DNS re-resolve), "
                                     "triggering reconnect");
                            int rfd2 = atomic_load_explicit(
                                &p->remote_fd, memory_order_acquire);
                            if (rfd2 >= 0) {
                                atomic_store_explicit(&p->reconnect_needed, 1,
                                                      memory_order_relaxed);
                                shutdown(rfd2, SHUT_RDWR);
                            }
                            remote_silent_count = 0;
                        }
                    } else {
                        remote_silent_count = 0;
                    }
                } else {
                    remote_silent_count = 0;
                    inactive_count++;
                    if (inactive_count >= checks_needed) {
                        log_info("remote timeout, triggering reconnect");
                        int rfd2 = atomic_load_explicit(&p->remote_fd,
                                                        memory_order_acquire);
                        if (rfd2 >= 0) {
                            atomic_store_explicit(&p->reconnect_needed, 1,
                                                  memory_order_relaxed);
                            shutdown(rfd2, SHUT_RDWR);
                        }
                        inactive_count = 0;
                    }
                }
            }
        }
    }

    close(epfd);
    return 0;
}
