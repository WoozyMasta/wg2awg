#ifndef AWG_PROXY_EMIT_H
#define AWG_PROXY_EMIT_H

#include "proxy.h"

int proxy_emit_send_packet(int fd, const void *data, int len);
int proxy_emit_send_packet_to(int fd, const void *data, int len,
                              const struct sockaddr_storage *addr,
                              socklen_t addr_len);
void proxy_emit_send_junk_and_cps(proxy_t *p, int fd);
void proxy_emit_send_junk_and_cps_to(proxy_t *p, int fd,
                                     const struct sockaddr_storage *addr,
                                     socklen_t addr_len);

/* Morph Mode: send junk using an explicit config (morph per-slot cfg).
 * Use alongside proxy_emit_send_junk_and_cps when morph_enabled, since the
 * base cfg has jc=0 and the morph cfg drives junk generation. */
void proxy_emit_send_junk_cfg(proxy_t *p, int fd, const awg_config_t *jcfg);
void proxy_emit_send_junk_cfg_to(proxy_t *p, int fd, const awg_config_t *jcfg,
                                 const struct sockaddr_storage *addr,
                                 socklen_t addr_len);

#endif
