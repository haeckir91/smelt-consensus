
#ifdef BARRELFISH
#include <barrelfish/barrelfish.h>
#endif
#include <string.h>
#include <numa.h>
#include <cassert>

#include "ff_queue.h"
#include "ffq_conf.h"

#include "parse_int.h"
#include "lxaffnuma.h"
#include "misc.h"
#include "sync.h"

// default values for ffq_peer_conf
#define DEFAULT_CORE_ID       -1
#define DEFAULT_QSIZE      (1024)
#define DEFAULT_SHM_NUMA_NODE -1
#define DEFAULT_DELAY          0

void
ffq_pair_conf_print(struct ffq_pair_conf *pair)
{
    printf("  SRC: core_id:%3d delay:%lu\n", pair->src.core_id, pair->src.delay);
    printf("  DST: core_id:%3d delay:%lu\n", pair->dst.core_id, pair->dst.delay);
    printf("  qsize:%lu shm_numa_node:%d\n", pair->qsize, pair->shm_numa_node);
}

#define DEFAULT_PEER_TUPLE {DEFAULT_CORE_ID, DEFAULT_DELAY}

static void
parse_peer_opts(const char *str, struct ffq_peer_conf *opts)
{
    int tuple[2] = DEFAULT_PEER_TUPLE;
    parse_int_tuple(str, tuple, sizeof(tuple) / sizeof(int));
    opts->core_id       = tuple[0];
    opts->delay         = tuple[1];

    // default (-1): allocate CPUS round-robin
    static int next_cpu = 0;
    if (opts->core_id == -1)
        opts->core_id = next_cpu++ % aff_get_ncpus();
}

#define DEFAULT_PAIR_TUPLE {DEFAULT_QSIZE, DEFAULT_SHM_NUMA_NODE}

static void
parse_pair_opts(const char *str, struct ffq_pair_conf *conf)
{
    int tuple[2] = DEFAULT_PAIR_TUPLE;
    parse_int_tuple(str, tuple, sizeof(tuple) / sizeof(int));
    conf->qsize         = tuple[0];
    conf->shm_numa_node = tuple[1];

    // default (-1): allocate on local src node
    if (conf->shm_numa_node == -1)
        conf->shm_numa_node = numa_cpu_to_node(conf->src.core_id);
}

// pair is in the form of X:Y:Z
struct ffq_pair_conf *
ffq_pair_conf_parse(const char *s)
{
    struct ffq_pair_conf *ret;
    int idxs[2];

    // do a copy
    size_t s_size = strlen(s) + 1;
    char str[s_size];
    memcpy(str, s, s_size);

    if (tokenize_by_sep(str, ':', idxs, sizeof(idxs) / sizeof(int)) < 0) {
            fprintf(stderr, "each pair is configured by a X:Y:Z tuple\n");
            fprintf(stderr, "given input: -->%s<--\n", s);
            exit(1);
    }

    ret = (struct ffq_pair_conf*) xmalloc(sizeof(*ret));
    ret->next = NULL;
    parse_peer_opts(str,           &ret->src);
    parse_peer_opts(str + idxs[0], &ret->dst);
    parse_pair_opts(str + idxs[1], ret);

    return ret;
}

struct ffq_pair_state *
ffq_pair_state_create(struct ffq_pair_conf *conf)
{
    struct ffq_pair_state *ret;
    struct ffq *ffq;

    ret = (struct ffq_pair_state*) xmalloc(sizeof(*ret));

    if (conf->shm_numa_node==-1) {

        coreid_t nidx = numa_node_of_cpu(conf->src.core_id);
        debug_printfff(DBG__FFQ,
                       "ffq: defaulting buffer location to node %d\n", nidx);
        conf->shm_numa_node = nidx;
    }

    debug_printfff(DBG__FFQ,
                   "ffq_pair_state_create: creating FFQ queue on node %d\n",
                   conf->shm_numa_node);

#ifdef BARRELFISH
    ffq = (struct ffq*) numa_alloc_onnode(ffq_size(conf->qsize),
                                              conf->shm_numa_node,
                                              BASE_PAGE_SIZE);
#else
    ffq = (struct ffq*) numa_alloc_onnode(ffq_size(conf->qsize),
                                              conf->shm_numa_node);
#endif
    assert (ffq!=NULL);

    ffq_init(ffq, conf->qsize);

    ffq_src_init(&ret->src, ffq);
    ffq_dst_init(&ret->dst, ffq);

    return ret;
}


void ffq_pair_conf_help(void)
{
        printf("  pair -> src_conf:dst_conf:global_conf\n");
        printf("  (src|dst)_conf -> <core_id,delay>\n");
        printf("  (global)_conf  -> <qsize,shm_numa_node>\n");
        printf("  DEFAULTS:\n");
        printf("    core_id       == -1 -> assign cores round robin\n");
        printf("    shm_numa_node == -1 -> allocate on local node\n");
        printf("    delay         ==  0 -> delay for each channel operation (cycles)\n");
}
