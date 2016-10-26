#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>

#include "pt_queue.h"

#include "misc.h"
#include "parse_int.h"
#include "lxaffnuma.h"

// default values for ptq_peer_conf
#define DEFAULT_CORE_ID       -1
#define DEFAULT_QSIZE      (1024)
#define DEFAULT_SHM_NUMA_NODE -1
#define DEFAULT_DELAY          0

// ptq peer configuration
struct ptq_peer_conf {
    int core_id;       // if -1, assign cores round-robin
    unsigned long delay;
};

// configuration for a ptq pair
struct ptq_pair_conf {
    struct ptq_peer_conf src, dst;
    struct ptq_pair_conf *next;
    size_t qsize;
    int    shm_numa_node; // if -1, local node of src is used
};

// test consists of a set of ptq pairs kept here
struct ptq_bench_conf {
    struct ptq_pair_conf *head;
    unsigned pairs_nr;
};

/*************************************************************************
 *
 * Parse arguments and generate configuration
 *
 ***********************************************/

static void
print_pair(struct ptq_pair_conf *pair)
{
    printf("  SRC: core_id:%3d delay:%lu\n", pair->src.core_id, pair->src.delay);
    printf("  DST: core_id:%3d delay:%lu\n", pair->dst.core_id, pair->dst.delay);
    printf("  qsize:%lu shm_numa_node:%d\n", pair->qsize, pair->shm_numa_node);
}

static void
print_conf(struct ptq_bench_conf *conf)
{
    printf("------- %u pair(s) ------\n", conf->pairs_nr);
    struct ptq_pair_conf *pair = conf->head;
    for (unsigned i=0; i<conf->pairs_nr; i++) {
        if (i != 0) printf("\n");
        print_pair(pair);
        pair = pair->next;
    }
    assert(pair == NULL);
}

#define DEFAULT_PEER_TUPLE {DEFAULT_CORE_ID, DEFAULT_DELAY}

static void
parse_peer_opts(const char *str, struct ptq_peer_conf *opts)
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
parse_pair_opts(const char *str, struct ptq_pair_conf *conf)
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
static struct ptq_pair_conf *
parse_pair(const char *s)
{
    struct ptq_pair_conf *ret;
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

    ret = xmalloc(sizeof(*ret));
    ret->next = NULL;
    parse_peer_opts(str,           &ret->src);
    parse_peer_opts(str + idxs[0], &ret->dst);
    parse_pair_opts(str + idxs[1], ret);

    return ret;
}

static struct ptq_bench_conf *
parse_options(int argc, const char *argv[])
{
    struct ptq_bench_conf *ret;
    ret = xmalloc(sizeof(*ret));

    ret->head = NULL;
    ret->pairs_nr = 0;
    struct ptq_pair_conf **next_ptr = &ret->head;

    for (unsigned i=1; i<argc; i++) {
        *next_ptr = parse_pair(argv[i]);
        ret->pairs_nr++;
        next_ptr = &((*next_ptr)->next);
    }

    return ret;
}

/*************************************************************************
 *
 * Pthreads and main()
 *
 *****************************/

#include <pthread.h>
#include "tsc.h"

#define COUNT 100000000 // message count for each thread

enum {
    THR_BENCH_SRC,
    THR_BENCH_DST
};

struct thr_arg {
    struct ptq_peer_conf  *conf;
    struct ptq            *queue;
    pthread_barrier_t     *tbar;
    int                    type;

    tsc_t                  timer;
};

static void
bench_src_thread(struct ptq *q, size_t cycles_delay)
{
    uintptr_t val = 0;
    for (size_t i=0; i<COUNT; i++) {
        ptq_enqueue(q, val++);
        tsc_spinticks(cycles_delay);
    }
}

static void
bench_dst_thread(struct ptq *q, size_t cycles_delay)
{
    uintptr_t val;
    for (size_t i=0; i<COUNT; i++) {
        ptq_dequeue(q, &val);
        assert(val == i);
        tsc_spinticks(cycles_delay);
    }
}

void *
thread_fn(void *arg)
{
    struct thr_arg *targ = (struct thr_arg *)arg;
    aff_set_oncpu(targ->conf->core_id);
    tsc_init(&targ->timer);

    // first barrier is for initialization
    pthread_barrier_wait(targ->tbar);

    tsc_start(&targ->timer);
    switch (targ->type) {
        case THR_BENCH_SRC:
        bench_src_thread(targ->queue, targ->conf->delay);
        break;

        case THR_BENCH_DST:
        bench_dst_thread(targ->queue, targ->conf->delay);
        break;

        default:
        printf("Unknown thread type:%d\n", targ->type);
        abort();
    }
    tsc_pause(&targ->timer);

    // second barrier is for the main thread to measure time
    pthread_barrier_wait(targ->tbar);

    return NULL;
}

int main(int argc, const char *argv[])
{

    // create a configuration based on arguments
    struct ptq_bench_conf *conf;
    conf = parse_options(argc, argv);

    if (conf->head == NULL) {
        printf("Usage: %s [pairs]\n", argv[0]);
        printf("  pair -> src_conf:dst_conf:global_conf\n");
        printf("  (src|dst)_conf -> <core_id,delay>\n");
        printf("  (global)_conf  -> <qsize,shm_numa_node>\n");
        printf("  DEFAULTS:\n");
        printf("    core_id       == -1 -> assign cores round robin\n");
        printf("    shm_numa_node == -1 -> allocate on local node\n");
        printf("    delay         ==  0 -> delay for each channel operation (cycles)\n");
        exit(0);
    }
    print_conf(conf);

    // initialize PTQ state (i.e., chans/queues) based on the configuration
    struct ptq *queues[conf->pairs_nr];
    struct ptq_pair_conf  *pair_conf = conf->head;
    for (unsigned int i=0; i<conf->pairs_nr; i++) {
        size_t size = ptq_size(pair_conf->qsize);
        queues[i] = numa_do_alloc(size, pair_conf->shm_numa_node);
        ptq_init(queues[i], pair_conf->qsize);
        pair_conf = pair_conf->next;
    }

    // initialize and start threads for each pair
    int               nthreads = conf->pairs_nr<<1;
    pthread_t         tids[nthreads];
    struct thr_arg    targs[nthreads];
    pthread_barrier_t tbar;

    pthread_barrier_init(&tbar, NULL, nthreads+1);

    pair_conf = conf->head;
    for (unsigned int i=0; i<conf->pairs_nr; i++) {
        struct ptq *q = queues[i];
        struct thr_arg *src_targ = targs + (i<<1);
        struct thr_arg *dst_targ = src_targ++;

        src_targ->conf  = &pair_conf->src;
        src_targ->queue = q;
        src_targ->type  = THR_BENCH_SRC;
        src_targ->tbar  = &tbar;

        dst_targ->conf  = &pair_conf->dst;
        dst_targ->queue = q;
        dst_targ->type  = THR_BENCH_DST;
        dst_targ->tbar  = &tbar;

        pair_conf = pair_conf->next;
    }
    assert(pair_conf == NULL);

    for (unsigned int i=0; i<nthreads; i++) {
        pthread_create(tids + i, NULL, thread_fn, targs + i);
    }

    tsc_t tsc; tsc_init(&tsc);
    // all threads initialize themselves and execute a barrier when ready
    pthread_barrier_wait(&tbar);
    tsc_start(&tsc);
    // all threads call a barrier before they finish
    pthread_barrier_wait(&tbar);
    tsc_pause(&tsc);

    // join threads
    for (unsigned int i=0; i<nthreads; i++) {
        pthread_join(tids[i], NULL);
    }

    uint64_t ticks = tsc_getticks(&tsc);
	printf("total:%s ticks [%s ticks/msg]\n", ul_hstr(ticks), ul_hstr(ticks/COUNT));
}
