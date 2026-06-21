#include "proxy_emit.h"
#include "cps.h"
#include "log.h"
#include <errno.h>
#include <string.h>

static int checked_mul_size(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a)
        return -1;
    *out = a * b;
    return 0;
}

static int junk_layout_sizes(const awg_config_t *cfg, size_t *junk_bytes,
                             size_t *junk_sizes_bytes) {
    if (checked_mul_size((size_t)cfg->jc, (size_t)cfg->jmax, junk_bytes) < 0)
        return -1;
    if (checked_mul_size((size_t)cfg->jc, sizeof(int), junk_sizes_bytes) < 0)
        return -1;
    return 0;
}

/* EPIPE/EBADF on the remote socket are expected while a reconnect is tearing it
 * down (control loop shutdown() then close()); they are not real send errors.
 */
static int send_err_is_reconnect_noise(int err) {
    return err == EPIPE || err == EBADF || err == ENOTCONN;
}

int proxy_emit_send_packet(int fd, const void *data, int len) {
    int r = (int)send(fd, data, len, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (r < 0 && !send_err_is_reconnect_noise(errno))
        log_debug2("send_packet failed: ", strerror(errno));
    return r;
}

int proxy_emit_send_packet_to(int fd, const void *data, int len,
                              const struct sockaddr_storage *addr,
                              socklen_t addr_len) {
    int r = (int)sendto(fd, data, len, MSG_DONTWAIT | MSG_NOSIGNAL,
                        (const struct sockaddr *)addr, addr_len);
    if (r < 0 && !send_err_is_reconnect_noise(errno))
        log_debug2("send_packet_to failed: ", strerror(errno));
    return r;
}

void proxy_emit_send_junk_and_cps_to(proxy_t *p, int fd,
                                     const struct sockaddr_storage *addr,
                                     socklen_t addr_len) {
    awg_config_t *cfg = p->cfg;

    int ncps =
        cps_generate_all(cfg->cps, &p->cps_counter, p->cps_bufs, p->cps_lens);
    for (int i = 0; i < ncps; i++)
        proxy_emit_send_packet_to(fd, p->cps_bufs[i], p->cps_lens[i], addr,
                                  addr_len);

    if (cfg->jc > 0 && cfg->jmax > 0) {
        size_t junk_bytes;
        size_t junk_sizes_bytes;
        if (junk_layout_sizes(cfg, &junk_bytes, &junk_sizes_bytes) < 0)
            return;
        (void)junk_sizes_bytes;
        fastrand_fill(&p->rng, p->junk_buf, junk_bytes);
        int njunk = generate_junk(cfg, p->junk_buf, p->junk_sizes);
        size_t off = 0;
        for (int i = 0; i < njunk; i++) {
            proxy_emit_send_packet_to(fd, p->junk_buf + off, p->junk_sizes[i],
                                      addr, addr_len);
            off += (size_t)p->junk_sizes[i];
        }
    }
}

void proxy_emit_send_junk_and_cps(proxy_t *p, int fd) {
    awg_config_t *cfg = p->cfg;

    int ncps =
        cps_generate_all(cfg->cps, &p->cps_counter, p->cps_bufs, p->cps_lens);
    for (int i = 0; i < ncps; i++)
        proxy_emit_send_packet(fd, p->cps_bufs[i], p->cps_lens[i]);

    if (cfg->jc > 0 && cfg->jmax > 0) {
        size_t junk_bytes;
        size_t junk_sizes_bytes;
        if (junk_layout_sizes(cfg, &junk_bytes, &junk_sizes_bytes) < 0)
            return;
        (void)junk_sizes_bytes;
        fastrand_fill(&p->rng, p->junk_buf, junk_bytes);
        int njunk = generate_junk(cfg, p->junk_buf, p->junk_sizes);
        size_t off = 0;
        for (int i = 0; i < njunk; i++) {
            proxy_emit_send_packet(fd, p->junk_buf + off, p->junk_sizes[i]);
            off += (size_t)p->junk_sizes[i];
        }
    }
}

void proxy_emit_send_junk_cfg(proxy_t *p, int fd, const awg_config_t *jcfg) {
    if (!jcfg || jcfg->jc <= 0 || jcfg->jmax <= 0)
        return;
    size_t junk_bytes = (size_t)jcfg->jc * (size_t)jcfg->jmax;
    fastrand_fill(&p->rng, p->junk_buf, junk_bytes);
    int njunk = generate_junk(jcfg, p->junk_buf, p->junk_sizes);
    size_t off = 0;
    for (int i = 0; i < njunk; i++) {
        proxy_emit_send_packet(fd, p->junk_buf + off, p->junk_sizes[i]);
        off += (size_t)p->junk_sizes[i];
    }
}

void proxy_emit_send_junk_cfg_to(proxy_t *p, int fd, const awg_config_t *jcfg,
                                 const struct sockaddr_storage *addr,
                                 socklen_t addr_len) {
    if (!jcfg || jcfg->jc <= 0 || jcfg->jmax <= 0)
        return;
    size_t junk_bytes = (size_t)jcfg->jc * (size_t)jcfg->jmax;
    fastrand_fill(&p->rng, p->junk_buf, junk_bytes);
    int njunk = generate_junk(jcfg, p->junk_buf, p->junk_sizes);
    size_t off = 0;
    for (int i = 0; i < njunk; i++) {
        proxy_emit_send_packet_to(fd, p->junk_buf + off, p->junk_sizes[i], addr,
                                  addr_len);
        off += (size_t)p->junk_sizes[i];
    }
}
