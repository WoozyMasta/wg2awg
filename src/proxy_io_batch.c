#include "proxy_io_batch.h"
#include "log.h"
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

int proxy_io_enable_gro(int fd) {
    int val = 1;
    return setsockopt(fd, IPPROTO_UDP, UDP_GRO, &val, sizeof(val)) == 0;
}

void proxy_io_init_gro_state(proxy_t *p) {
    p->gro_iov.iov_base = p->gro_buf;
    p->gro_iov.iov_len = GRO_BUF_SIZE;
    memset(&p->gro_hdr, 0, sizeof(p->gro_hdr));
    p->gro_hdr.msg_iov = &p->gro_iov;
    p->gro_hdr.msg_iovlen = 1;
    p->gro_hdr.msg_control = p->gro_cmsg;
    p->gro_hdr.msg_controllen = sizeof(p->gro_cmsg);

    p->gro_iov_c2s.iov_base = p->gro_buf_c2s;
    p->gro_iov_c2s.iov_len = GRO_BUF_SIZE;
    memset(&p->gro_hdr_c2s, 0, sizeof(p->gro_hdr_c2s));
    p->gro_hdr_c2s.msg_iov = &p->gro_iov_c2s;
    p->gro_hdr_c2s.msg_iovlen = 1;
    p->gro_hdr_c2s.msg_control = p->gro_cmsg_c2s;
    p->gro_hdr_c2s.msg_controllen = sizeof(p->gro_cmsg_c2s);
    p->gro_hdr_c2s.msg_name = &p->gro_addr_c2s;
    p->gro_hdr_c2s.msg_namelen = sizeof(struct sockaddr_storage);
}

int proxy_io_recv_gro(proxy_t *p, int fd, int *seg_size) {
    p->gro_hdr.msg_controllen = sizeof(p->gro_cmsg);
    p->gro_hdr.msg_flags = 0;

    ssize_t n = recvmsg(fd, &p->gro_hdr, 0);
    if (n <= 0) {
        *seg_size = 0;
        return (int)n;
    }

    *seg_size = 0;
    for (struct cmsghdr *cm = CMSG_FIRSTHDR(&p->gro_hdr); cm;
         cm = CMSG_NXTHDR(&p->gro_hdr, cm)) {
        if (cm->cmsg_level == IPPROTO_UDP && cm->cmsg_type == UDP_SEGMENT) {
            uint16_t ss;
            memcpy(&ss, CMSG_DATA(cm), sizeof(ss));
            *seg_size = ss;
            break;
        }
    }

    return (int)n;
}

static int send_gso(int fd, struct iovec *iovecs, int count,
                    const struct sockaddr_storage *addr, socklen_t addr_len) {
    if (count <= 1)
        return 0;

    int seg_size = (int)iovecs[0].iov_len;
    int gso_count = 1;
    while (gso_count < count && (int)iovecs[gso_count].iov_len == seg_size)
        gso_count++;
    if (gso_count < count && (int)iovecs[gso_count].iov_len < seg_size)
        gso_count++;
    if (gso_count <= 1)
        return 0;

    union {
        char buf[CMSG_SPACE(sizeof(uint16_t))];
        struct cmsghdr align;
    } cmsg_u;
    memset(&cmsg_u, 0, sizeof(cmsg_u));

    struct msghdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msg_iov = iovecs;
    hdr.msg_iovlen = gso_count;
    hdr.msg_control = cmsg_u.buf;
    hdr.msg_controllen = sizeof(cmsg_u.buf);

    struct cmsghdr *cm = CMSG_FIRSTHDR(&hdr);
    cm->cmsg_level = IPPROTO_UDP;
    cm->cmsg_type = UDP_SEGMENT;
    cm->cmsg_len = CMSG_LEN(sizeof(uint16_t));
    uint16_t ss = (uint16_t)seg_size;
    memcpy(CMSG_DATA(cm), &ss, sizeof(ss));

    if (addr) {
        hdr.msg_name = (void *)addr;
        hdr.msg_namelen = addr_len;
    }

    ssize_t ret = sendmsg(fd, &hdr, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (ret < 0)
        return -errno;
    return gso_count;
}

void proxy_io_send_batch_gso(proxy_t *p, int fd, struct mmsghdr *msgs,
                             struct iovec *iovecs, int nsend,
                             const struct sockaddr_storage *addr,
                             socklen_t addr_len) {
    int sent = 0;
    if (p->gso_ok && nsend > 1 && (addr || fd != p->listen_fd)) {
        int n = send_gso(fd, iovecs, nsend, addr, addr_len);
        if (n < 0) {
            int err = -n;
            if (err == ENOPROTOOPT || err == EIO)
                p->gso_ok = 0;
        } else {
            sent = n;
        }
    }
    while (sent < nsend) {
        int r = sendmmsg(fd, msgs + sent, nsend - sent, MSG_NOSIGNAL);
        if (r <= 0) {
            log_debug2("sendmmsg failed: ", strerror(errno));
            break;
        }
        sent += r;
    }
}
