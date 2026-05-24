#include "session_table.h"
#include <string.h>

void session_addr_copy(struct sockaddr_storage *dst, socklen_t *dst_len,
                       const struct sockaddr_storage *src, socklen_t src_len) {
    if (src_len > (socklen_t)sizeof(*dst))
        src_len = (socklen_t)sizeof(*dst);
    memcpy(dst, src, (size_t)src_len);
    *dst_len = src_len;
}

int session_addr_equal(const struct sockaddr_storage *a, socklen_t alen,
                       const struct sockaddr_storage *b, socklen_t blen) {
    (void)alen;
    (void)blen;
    if (a->ss_family != b->ss_family)
        return 0;
    if (a->ss_family == AF_INET) {
        const struct sockaddr_in *ia = (const struct sockaddr_in *)a;
        const struct sockaddr_in *ib = (const struct sockaddr_in *)b;
        return ia->sin_port == ib->sin_port &&
               ia->sin_addr.s_addr == ib->sin_addr.s_addr;
    }
    if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6 *ia = (const struct sockaddr_in6 *)a;
        const struct sockaddr_in6 *ib = (const struct sockaddr_in6 *)b;
        return ia->sin6_port == ib->sin6_port &&
               ia->sin6_scope_id == ib->sin6_scope_id &&
               memcmp(&ia->sin6_addr, &ib->sin6_addr, sizeof(ia->sin6_addr)) ==
                   0;
    }
    return 0;
}

session_entry_t *session_get_entry(session_entry_t *sessions, uint32_t index) {
    uint32_t slot = index & SESSION_TABLE_MASK;
    for (int i = 0; i < 4; i++) {
        uint32_t s = (slot + i) & SESSION_TABLE_MASK;
        if (atomic_load_explicit(&sessions[s].valid, memory_order_acquire) &&
            atomic_load_explicit(&sessions[s].sender_index,
                                 memory_order_relaxed) == index)
            return &sessions[s];
    }
    return NULL;
}

int session_lookup(session_entry_t *sessions, uint32_t index,
                   session_entry_t **entry_out,
                   struct sockaddr_storage *addr_out, socklen_t *addr_len_out) {
    uint32_t slot = index & SESSION_TABLE_MASK;
    for (int i = 0; i < 4; i++) {
        uint32_t s = (slot + i) & SESSION_TABLE_MASK;
        session_entry_t *entry = &sessions[s];
        if (!atomic_load_explicit(&entry->valid, memory_order_acquire))
            continue;
        if (atomic_load_explicit(&entry->sender_index, memory_order_relaxed) !=
            index)
            continue;
        for (;;) {
            uint32_t seq1 =
                atomic_load_explicit(&entry->seq, memory_order_acquire);
            if (seq1 & 1u)
                continue;
            session_addr_copy(addr_out, addr_len_out, &entry->addr,
                              entry->addr_len);
            uint32_t seq2 =
                atomic_load_explicit(&entry->seq, memory_order_acquire);
            if (seq1 == seq2 && !(seq2 & 1u))
                break;
        }
        if (!atomic_load_explicit(&entry->valid, memory_order_acquire) ||
            atomic_load_explicit(&entry->sender_index, memory_order_relaxed) !=
                index)
            continue;
        *entry_out = entry;
        return 1;
    }
    return 0;
}

void session_put(session_entry_t *sessions, uint32_t index,
                 const struct sockaddr_storage *addr, socklen_t addr_len) {
    uint32_t slot = index & SESSION_TABLE_MASK;
    for (int i = 0; i < 4; i++) {
        uint32_t s = (slot + i) & SESSION_TABLE_MASK;
        session_entry_t *entry = &sessions[s];
        int was_valid =
            atomic_load_explicit(&entry->valid, memory_order_acquire);
        uint32_t cur_idx =
            atomic_load_explicit(&entry->sender_index, memory_order_relaxed);
        if (!was_valid || cur_idx == index) {
            int preserve_peer = was_valid && cur_idx == index;
            atomic_store_explicit(&entry->valid, 0, memory_order_release);
            atomic_fetch_add_explicit(&entry->seq, 1u, memory_order_acq_rel);
            atomic_store_explicit(&entry->sender_index, index,
                                  memory_order_relaxed);
            session_addr_copy(&entry->addr, &entry->addr_len, addr, addr_len);
            if (!preserve_peer)
                atomic_store_explicit(&entry->peer_slot, -1,
                                      memory_order_relaxed);
            atomic_fetch_add_explicit(&entry->seq, 1u, memory_order_release);
            atomic_store_explicit(&entry->valid, 1, memory_order_release);
            return;
        }
    }
    atomic_store_explicit(&sessions[slot].valid, 0, memory_order_release);
    atomic_fetch_add_explicit(&sessions[slot].seq, 1u, memory_order_acq_rel);
    atomic_store_explicit(&sessions[slot].sender_index, index,
                          memory_order_relaxed);
    session_addr_copy(&sessions[slot].addr, &sessions[slot].addr_len, addr,
                      addr_len);
    atomic_store_explicit(&sessions[slot].peer_slot, -1, memory_order_relaxed);
    atomic_fetch_add_explicit(&sessions[slot].seq, 1u, memory_order_release);
    atomic_store_explicit(&sessions[slot].valid, 1, memory_order_release);
}

int session_get(session_entry_t *sessions, uint32_t index,
                struct sockaddr_storage *addr_out, socklen_t *addr_len_out) {
    session_entry_t *entry = NULL;
    return session_lookup(sessions, index, &entry, addr_out, addr_len_out);
}

void session_set_peer_slot(session_entry_t *entry, int peer_slot) {
    if (entry)
        atomic_store_explicit(&entry->peer_slot, peer_slot,
                              memory_order_relaxed);
}

void session_set_peer_slot_if_index(session_entry_t *entry,
                                    uint32_t expected_index, int peer_slot) {
    if (!entry)
        return;
    if (!atomic_load_explicit(&entry->valid, memory_order_acquire))
        return;
    if (atomic_load_explicit(&entry->sender_index, memory_order_relaxed) !=
        expected_index)
        return;
    atomic_store_explicit(&entry->peer_slot, peer_slot, memory_order_relaxed);
}

int session_get_peer_slot(session_entry_t *entry) {
    return entry ? atomic_load_explicit(&entry->peer_slot, memory_order_relaxed)
                 : -1;
}

int session_find_sole_entry_with_addr(session_entry_t *sessions,
                                      session_entry_t **entry_out,
                                      struct sockaddr_storage *addr_out,
                                      socklen_t *addr_len_out) {
    int found_idx = -1;
    struct sockaddr_storage found_addr;
    socklen_t found_addr_len = 0;
    for (int i = 0; i < SESSION_TABLE_SIZE; i++) {
        session_entry_t *entry = &sessions[i];
        if (!atomic_load_explicit(&entry->valid, memory_order_acquire))
            continue;
        uint32_t idx =
            atomic_load_explicit(&entry->sender_index, memory_order_relaxed);
        struct sockaddr_storage addr;
        socklen_t addr_len;
        for (;;) {
            uint32_t seq1 =
                atomic_load_explicit(&entry->seq, memory_order_acquire);
            if (seq1 & 1u)
                continue;
            session_addr_copy(&addr, &addr_len, &entry->addr, entry->addr_len);
            uint32_t seq2 =
                atomic_load_explicit(&entry->seq, memory_order_acquire);
            if (seq1 == seq2 && !(seq2 & 1u))
                break;
        }
        if (!atomic_load_explicit(&entry->valid, memory_order_acquire) ||
            atomic_load_explicit(&entry->sender_index, memory_order_relaxed) !=
                idx)
            continue;
        if (found_idx < 0) {
            found_idx = i;
            found_addr = addr;
            found_addr_len = addr_len;
        } else if (!session_addr_equal(&found_addr, found_addr_len, &addr,
                                       addr_len)) {
            return 0;
        }
    }
    if (found_idx < 0)
        return 0;
    *addr_out = found_addr;
    *addr_len_out = found_addr_len;
    *entry_out = &sessions[found_idx];
    return 1;
}

session_entry_t *session_find_sole_entry(session_entry_t *sessions) {
    session_entry_t *entry = NULL;
    struct sockaddr_storage addr;
    socklen_t addr_len;
    return session_find_sole_entry_with_addr(sessions, &entry, &addr, &addr_len)
               ? entry
               : NULL;
}

int session_find_sole_client(session_entry_t *sessions,
                             struct sockaddr_storage *addr_out,
                             socklen_t *addr_len_out) {
    session_entry_t *entry = NULL;
    return session_find_sole_entry_with_addr(sessions, &entry, addr_out,
                                             addr_len_out);
}

session_entry_t *session_find_by_addr(session_entry_t *sessions,
                                      const struct sockaddr_storage *addr,
                                      socklen_t addr_len) {
    for (int i = 0; i < SESSION_TABLE_SIZE; i++) {
        session_entry_t *entry = &sessions[i];
        if (!atomic_load_explicit(&entry->valid, memory_order_acquire))
            continue;
        struct sockaddr_storage cur_addr;
        socklen_t cur_len;
        for (;;) {
            uint32_t seq1 =
                atomic_load_explicit(&entry->seq, memory_order_acquire);
            if (seq1 & 1u)
                continue;
            session_addr_copy(&cur_addr, &cur_len, &entry->addr,
                              entry->addr_len);
            uint32_t seq2 =
                atomic_load_explicit(&entry->seq, memory_order_acquire);
            if (seq1 == seq2 && !(seq2 & 1u))
                break;
        }
        if (session_addr_equal(&cur_addr, cur_len, addr, addr_len))
            return entry;
    }
    return NULL;
}
