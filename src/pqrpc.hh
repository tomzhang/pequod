#ifndef PEQUOD_PQRPC_HH
#define PEQUOD_PQRPC_HH

enum {
    pq_get = 1,
    pq_insert = 2,
    pq_erase = 3,
    pq_count = 4,
    pq_scan = 5,
    pq_subscribe = 6,
    pq_add_join = 7,
    // order matters
    pq_stats = 8,
    pq_control = 9
};

enum {
    pq_ok = 0,
    pq_fail = -1
};

#endif
