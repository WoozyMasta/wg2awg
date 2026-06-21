#ifndef AWG_PROXY_H
#define AWG_PROXY_H

#include "transform.h"
#include "morph.h"
#include "fastrand.h"
#include "session_table.h"
#include "obfs.h"
#include <stdint.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUF_SIZE AWG_PACKET_BUF_SIZE
#define BATCH_SIZE 32
#define H4_RING_SIZE 4096
#define GRO_BUF_SIZE 65536

#ifndef UDP_GRO
#define UDP_GRO 104
#endif
#ifndef UDP_SEGMENT
#define UDP_SEGMENT 103
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif

typedef struct {
    /* Hot fields */
    awg_config_t *cfg;            /* 8B */
    int listen_fd;                /* 4B */
    _Atomic int remote_fd;        /* 4B */
    _Atomic int stopped;          /* 4B */
    _Atomic int has_client;       /* 4B */
    _Atomic int last_active;      /* 4B */
    _Atomic int last_remote_rx;   /* 4B - set when data received from remote */
    _Atomic int reconnect_needed; /* 4B */
    _Atomic int reconnect_drift;  /* 4B - bind a fresh src port on next dial */
    /* First-event flags (one per connection - touched once after
     * start/reconnect) */
    _Atomic uint8_t fe_init_seen;     /* WG handshake init seen from client */
    _Atomic uint8_t fe_init_sent;     /* AWG handshake init sent to remote */
    _Atomic uint8_t fe_remote_pkt;    /* any packet received from remote */
    _Atomic uint8_t fe_resp_received; /* AWG handshake response received */
    _Atomic uint8_t
        fe_resp_sent; /* WG handshake response delivered to client */
    _Atomic uint8_t fe_transport_c2s; /* first transport packet to remote */
    _Atomic uint8_t fe_transport_s2c; /* first transport packet to client */
    uint8_t _pad_fe;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len;
    int gso_ok;      /* 4B */
    int gro_enabled; /* 4B */
    uint16_t h4_idx; /* 2B */
    uint16_t _pad0;  /* 2B */
    fastrand_t rng;  /* 8B */

    /* Warm: batch I/O */

    /* Batch I/O buffers - c2s direction */
    struct {
        uint8_t bufs[BATCH_SIZE][BUF_SIZE + AWG_PACKET_HEADROOM];
        struct mmsghdr msgs[BATCH_SIZE];
        struct iovec iovecs[BATCH_SIZE];
        struct sockaddr_storage addrs[BATCH_SIZE];
    } recv_c2s;

    struct {
        struct mmsghdr msgs[BATCH_SIZE];
        struct iovec iovecs[BATCH_SIZE];
        uint8_t bufs[BATCH_SIZE][BUF_SIZE + AWG_PACKET_HEADROOM];
    } send_c2s;

    /* Batch I/O buffers - s2c direction (non-GRO path) */
    struct {
        uint8_t bufs[BATCH_SIZE][BUF_SIZE + AWG_PACKET_HEADROOM];
        struct mmsghdr msgs[BATCH_SIZE];
        struct iovec iovecs[BATCH_SIZE];
    } recv_s2c;

    struct {
        struct mmsghdr msgs[BATCH_SIZE];
        struct iovec iovecs[BATCH_SIZE];
        uint8_t bufs[BATCH_SIZE][BUF_SIZE + AWG_PACKET_HEADROOM];
        struct sockaddr_storage addrs[BATCH_SIZE];
        socklen_t addrlens[BATCH_SIZE];
    } send_s2c;

    /* Cold: init/reconnect */
    struct sockaddr_storage listen_addr;
    socklen_t listen_addr_len;
    struct sockaddr_storage remote_addr;
    socklen_t remote_addr_len;
    char remote_host[256];
    uint16_t remote_port;
    int auto_src_port;
    int local_port;
    int dns_resolve_fail_started_at;

    uint32_t cps_counter;
    uint8_t *junk_buf;
    int *junk_sizes;

    int signal_fd;
    int timer_fd;

    uint8_t cps_bufs[5][1500];
    int cps_lens[5];

    /* GRO state - s2c */
    uint8_t gro_buf[GRO_BUF_SIZE];
    struct iovec gro_iov;
    struct msghdr gro_hdr;
    uint8_t gro_cmsg[32];

    /* GRO state - c2s */
    uint8_t gro_buf_c2s[GRO_BUF_SIZE];
    struct iovec gro_iov_c2s;
    struct msghdr gro_hdr_c2s;
    uint8_t gro_cmsg_c2s[32];
    struct sockaddr_storage gro_addr_c2s;
    int gro_enabled_c2s;

    /* Large cold arrays */
    uint32_t h4_ring[H4_RING_SIZE];
    session_entry_t sessions[SESSION_TABLE_SIZE];
    obfs_session_t obfs_c2s;
    obfs_session_t obfs_s2c;
    uint32_t obfs_fail_c2s;
    uint32_t obfs_fail_s2c;

    /* Morph Mode: double-buffer snapshot for lock-free hot-path reads. */
    morph_state_t morph;

} proxy_t;

/* Initialize proxy. Returns 0 on success. */
int proxy_init(proxy_t *p, awg_config_t *cfg, const char *listen_str,
               const char *remote_str, int src_port);

/* Run proxy event loop. Blocks until signal or error. Returns 0 on clean
 * shutdown. */
int proxy_run(proxy_t *p);

#endif
