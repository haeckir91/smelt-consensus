#ifndef FFQ_CONF_H__
#define FFQ_CONF_H__

#include <stddef.h> // offsetof

#include "ff_queue.h"
#include "misc.h"

struct ffq_pair_state {
    struct ffq_src src CACHE_ALIGNED;
    struct ffq_dst dst CACHE_ALIGNED;
};

// ffq peer configuration
struct ffq_peer_conf {
    int core_id;       // if -1, assign cores round-robin
    unsigned long delay; // XXX Remove me
};

// configuration for a ffq pair
struct ffq_pair_conf {
    struct ffq_peer_conf src, dst;
    struct ffq_pair_conf *next;
    size_t qsize;
    int    shm_numa_node; // if -1, local node of src is used
};

void ffq_pair_conf_print(struct ffq_pair_conf *pair);
void ffq_pair_conf_help(void);

struct ffq_pair_conf *ffq_pair_conf_parse(const char *s);

struct ffq_pair_state *ffq_pair_state_create(struct ffq_pair_conf *conf);

#endif /* FFQ_CONF_H__ */
