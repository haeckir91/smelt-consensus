
#include "ump_conf.h"

#include <string.h>
#include "parse_int.h"
#include "ump_chan.h"
#include "ump_os.h"
#include "ump_queue.h"

/*************************************************************************
 *
 * OS-specific helpers for affinity/NUMA allocation
 *
 ********************************************************/
#ifdef BARRELFISH
#include <barrelfish/barrelfish.h>
#endif

#include <sched.h>
#include <unistd.h>
#include <numa.h>

#include "lxaffnuma.h"
#include "misc.h"

// default values for ump_peer_conf
#define DEFAULT_CORE_ID       -1
#define DEFAULT_SHM_SIZE      (4096*2)
#define DEFAULT_SHM_NUMA_NODE -1
#define DEFAULT_DELAY          0

#define DEFAULT_NONBLOCK      0
#define DEFAULT_SHARED_NUMA_NODE -1

struct ump_pair_state *
ump_pair_state_create(struct ump_pair_conf *conf)
{
    struct ump_pair_state *pair_state;
    struct ump_peer_state *src_state, *dst_state;
    struct ump_chan_shared *shared_state;
    void *src_shm, *dst_shm;
    bool ret1, ret2;

    pair_state = (struct ump_pair_state*) xmalloc(sizeof(*pair_state));
    memset(pair_state, 0, sizeof(*pair_state));
    if (conf->shared_numa_node == -1) {
        shared_state = (struct ump_chan_shared*) xmalloc(sizeof(*shared_state));
    } else {
#ifdef BARRELFISH
        shared_state = (struct ump_chan_shared*) numa_alloc_onnode(sizeof(*shared_state),
                                             conf->shared_numa_node, BASE_PAGE_SIZE);
#else
        shared_state = (struct ump_chan_shared*) numa_alloc_onnode(sizeof(*shared_state),
                                     conf->shared_numa_node);
#endif
        assert (shared_state!=NULL);
    }

    // Make sure at least one messages fits into the buffers we allocate
    assert (conf->src.shm_size >= UMP_MSG_BYTES);
    assert (conf->dst.shm_size >= UMP_MSG_BYTES);

    /* printf("Allocating %zu/%zu memory for src/dst\n", \ */
    /*        conf->src.shm_size, conf->dst.shm_size); */

#ifdef BARRELFISH
    src_shm = numa_alloc_onnode(conf->src.shm_size, conf->src.shm_numa_node, BASE_PAGE_SIZE);
    dst_shm = numa_alloc_onnode(conf->dst.shm_size, conf->dst.shm_numa_node, BASE_PAGE_SIZE);
#else
    src_shm = numa_alloc_onnode(conf->src.shm_size, conf->src.shm_numa_node);
    dst_shm = numa_alloc_onnode(conf->dst.shm_size, conf->dst.shm_numa_node);
#endif
    assert(src_shm!=NULL && dst_shm!=NULL);

    src_state = &pair_state->src;
    dst_state = &pair_state->dst;

    ump_wake_context_setup(&src_state->wake_ctx);
    ump_wake_context_setup(&dst_state->wake_ctx);

    ret1 = ump_chan_init_numa(&src_state->chan,
                              shared_state,
                              src_shm, conf->src.shm_size,
                              dst_shm, conf->dst.shm_size,
                              true,
                              src_state->wake_ctx);

    ret2 = ump_chan_init_numa(&dst_state->chan,
                              shared_state,
                              dst_shm, conf->dst.shm_size,
                              src_shm, conf->src.shm_size,
                              false,
                              dst_state->wake_ctx);

    if (!ret1 || !ret2) {
        fprintf(stderr, "ump_chan_init_numa failed\n");
        abort();
    }

    ump_queue_init(&src_state->queue, &src_state->chan, dst_state->wake_ctx);
    ump_queue_init(&dst_state->queue, &dst_state->chan, src_state->wake_ctx);

    return pair_state;
}

/*************************************************************************
 *
 * Parse arguments and generate configuration
 *
 ***********************************************/

#define DEFAULT_PEER_TUPLE \
    {DEFAULT_CORE_ID, DEFAULT_SHM_SIZE, DEFAULT_SHM_NUMA_NODE, DEFAULT_DELAY}

static void
parse_peer_opts(const char *str, struct ump_peer_conf *opts)
{
    int tuple[4] = DEFAULT_PEER_TUPLE;
    parse_int_tuple(str, tuple, sizeof(tuple) / sizeof(int));
    opts->core_id       = tuple[0];
    opts->shm_size      = tuple[1];
    opts->shm_numa_node = tuple[2];
    opts->delay         = tuple[3];

    // default (-1): allocate CPUS round-robin
    static int next_cpu = 0;
    if (opts->core_id == -1)
        opts->core_id = next_cpu++ % aff_get_ncpus();

    // default (-1): allocate on local node
    if (opts->shm_numa_node == -1)
        opts->shm_numa_node = 0;
}

#define DEFAULT_PAIR_TUPLE \
     {DEFAULT_NONBLOCK, DEFAULT_SHARED_NUMA_NODE}

static void
parse_pair_opts(const char *str, struct ump_pair_conf *conf)
{
    int tuple[2] = DEFAULT_PAIR_TUPLE;
    parse_int_tuple(str, tuple, sizeof(tuple) / sizeof(int));
    conf->nonblock         = tuple[0];
    conf->shared_numa_node = tuple[1];
}

// pair configuration is in the form of X:Y, where X Y are int tuples of size 3
struct ump_pair_conf *
ump_pair_conf_parse(const char *s)
{
    struct ump_pair_conf *ret;
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

    ret = (struct ump_pair_conf*) xmalloc(sizeof(*ret));
    ret->next = NULL;
    parse_peer_opts(str,           &ret->src);
    parse_peer_opts(str + idxs[0], &ret->dst);
    parse_pair_opts(str + idxs[1], ret);

    return ret;
}

void
ump_pair_conf_help(void)
{
        printf("  pair -> src_conf:dst_conf:pair_conf\n");
        printf("  {src,dst}_conf -> <core_id,shm_size,shm_numa_node,delay>\n");
        printf("  pair_conf      -> <nonblock,shm_numa_node>\n");
        printf("  peer (src,dst) DEFAULTS:\n");
        printf("    core_id       == -1 -> assign cores round robin\n");
        printf("    shm_numa_node == -1 -> allocate on local node\n");
        printf("    delay         ==  0 -> delay for each channel operation (cycles)\n");
        printf("  pair DEFAULTS:\n");
        printf("    nonblock        ==  0 -> use non-blocking functions \n");
        printf("    shared_numa_node == -1 -> use malloc() \n");
}

