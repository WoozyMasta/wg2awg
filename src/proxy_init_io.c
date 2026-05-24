#include "proxy_init_io.h"
#include "proxy_io_batch.h"
#include "fastrand.h"

void proxy_init_io_state(proxy_t *p) {
    awg_config_t *cfg = p->cfg;

    int c2s_headroom = (cfg->mode == AWG_MODE_CLIENT) ? cfg->s4 : 0;
    int c2s_recv_len = (cfg->mode == AWG_MODE_CLIENT)
                           ? BUF_SIZE
                           : (BUF_SIZE + AWG_PACKET_HEADROOM);
    int s2c_headroom = (cfg->mode == AWG_MODE_CLIENT) ? 0 : cfg->s4;

    for (int i = 0; i < BATCH_SIZE; i++) {
        p->recv_c2s.iovecs[i].iov_base = p->recv_c2s.bufs[i] + c2s_headroom;
        p->recv_c2s.iovecs[i].iov_len = c2s_recv_len;
        p->recv_c2s.msgs[i].msg_hdr.msg_iov = &p->recv_c2s.iovecs[i];
        p->recv_c2s.msgs[i].msg_hdr.msg_iovlen = 1;
    }

    if (cfg->mode != AWG_MODE_CLIENT) {
        for (int i = 0; i < BATCH_SIZE; i++) {
            p->recv_c2s.msgs[i].msg_hdr.msg_name = &p->recv_c2s.addrs[i];
            p->recv_c2s.msgs[i].msg_hdr.msg_namelen =
                sizeof(struct sockaddr_storage);
        }
    } else {
        p->recv_c2s.msgs[0].msg_hdr.msg_name = &p->recv_c2s.addrs[0];
        p->recv_c2s.msgs[0].msg_hdr.msg_namelen =
            sizeof(struct sockaddr_storage);
    }

    for (int i = 0; i < BATCH_SIZE; i++) {
        p->send_s2c.msgs[i].msg_hdr.msg_iov = &p->send_s2c.iovecs[i];
        p->send_s2c.msgs[i].msg_hdr.msg_iovlen = 1;
        p->send_s2c.msgs[i].msg_hdr.msg_name = &p->send_s2c.addrs[i];
        p->send_s2c.msgs[i].msg_hdr.msg_namelen = 0;

        p->send_c2s.msgs[i].msg_hdr.msg_iov = &p->send_c2s.iovecs[i];
        p->send_c2s.msgs[i].msg_hdr.msg_iovlen = 1;
    }

    for (int i = 0; i < BATCH_SIZE; i++) {
        p->recv_s2c.iovecs[i].iov_base = p->recv_s2c.bufs[i] + s2c_headroom;
        p->recv_s2c.iovecs[i].iov_len =
            BUF_SIZE + AWG_PACKET_HEADROOM - s2c_headroom;
        p->recv_s2c.msgs[i].msg_hdr.msg_iov = &p->recv_s2c.iovecs[i];
        p->recv_s2c.msgs[i].msg_hdr.msg_iovlen = 1;
    }

    if (c2s_headroom > 0) {
        for (int i = 0; i < BATCH_SIZE; i++)
            fastrand_fill(&p->rng, p->recv_c2s.bufs[i], c2s_headroom);
    }
    if (s2c_headroom > 0) {
        for (int i = 0; i < BATCH_SIZE; i++)
            fastrand_fill(&p->rng, p->recv_s2c.bufs[i], s2c_headroom);
    }

    proxy_io_init_gro_state(p);
}
