#ifndef AWG_SESSION_TABLE_H
#define AWG_SESSION_TABLE_H

#include <stdint.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "obfs.h"

#define SESSION_TABLE_SIZE 4096
#define SESSION_TABLE_MASK (SESSION_TABLE_SIZE - 1)

typedef struct {
    _Atomic uint32_t sender_index;
    _Atomic uint32_t seq;
    _Atomic int peer_slot;
    _Atomic int valid;
    _Atomic int obfs_ready;
    obfs_session_t obfs_c2s;
    obfs_session_t obfs_s2c;
    socklen_t addr_len;
    struct sockaddr_storage addr;
} session_entry_t;

void session_addr_copy(struct sockaddr_storage *dst, socklen_t *dst_len,
                       const struct sockaddr_storage *src, socklen_t src_len);
int session_addr_equal(const struct sockaddr_storage *a, socklen_t alen,
                       const struct sockaddr_storage *b, socklen_t blen);

session_entry_t *session_get_entry(session_entry_t *sessions, uint32_t index);
int session_lookup(session_entry_t *sessions, uint32_t index,
                   session_entry_t **entry_out,
                   struct sockaddr_storage *addr_out, socklen_t *addr_len_out);
void session_put(session_entry_t *sessions, uint32_t index,
                 const struct sockaddr_storage *addr, socklen_t addr_len);
int session_get(session_entry_t *sessions, uint32_t index,
                struct sockaddr_storage *addr_out, socklen_t *addr_len_out);
void session_set_peer_slot(session_entry_t *entry, int peer_slot);
void session_set_peer_slot_if_index(session_entry_t *entry,
                                    uint32_t expected_index, int peer_slot);
int session_get_peer_slot(session_entry_t *entry);

int session_find_sole_entry_with_addr(session_entry_t *sessions,
                                      session_entry_t **entry_out,
                                      struct sockaddr_storage *addr_out,
                                      socklen_t *addr_len_out);
session_entry_t *session_find_sole_entry(session_entry_t *sessions);
int session_find_sole_client(session_entry_t *sessions,
                             struct sockaddr_storage *addr_out,
                             socklen_t *addr_len_out);
session_entry_t *session_find_by_addr(session_entry_t *sessions,
                                      const struct sockaddr_storage *addr,
                                      socklen_t addr_len);

#endif
