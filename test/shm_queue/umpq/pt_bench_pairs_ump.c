#include <stdio.h>
#include <pthread.h>

#include "ump_chan.h"
#include "ump_queue.h"
#include "ump_os.h"
#include "ump_conf.h"

#include "parse_int.h"
#include "lxaffnuma.h"
#include "tsc.h"

#include "pairwise_bench.h"

struct bench_conf {
    int *src, *dst;
    unsigned src_size, dst_size;
};

// bidirectional (i.e., with two channels) UMP pair
// fwr: forward src->dst
// rev: reverse dst->src
struct ump_bi_pair_state {
    struct ump_pair_state *fwr, *rev;
};

struct thr_arg {
    unsigned              core_id;
    struct ump_peer_state *send, *recv;
    pthread_barrier_t     *tbar;
    unsigned              count;

    tsc_t timer;
};

#include "measurement_framework.h"
extern __thread struct sk_measurement send;
extern __thread struct sk_measurement receive;

extern __thread uint64_t m_buf_send[NUM_REPETITIONS];
extern __thread uint64_t m_buf_receive[NUM_REPETITIONS];

extern __thread bool measure_receive;
extern __thread bool measure_send;

static void *
src_thr(void *arg)
{
    measure_send = true;
    sk_m_init(&send, NUM_REPETITIONS, "send", m_buf_send);

    struct thr_arg *targ = arg;

    aff_set_oncpu(targ->core_id);
    tsc_init(&targ->timer);

    // first barrier is for initialization
    pthread_barrier_wait(targ->tbar);

    uintptr_t val=0;
    bool __attribute__((unused)) ret;

    unsigned i;
    for (i=0; i<targ->count; i++) {

        // send
        ret = ump_enqueue_word_nonblock(&targ->send->queue, val);

        assert(ret == true); // only one message in-flight

        // receive
        uintptr_t rval;
        while (!ump_dequeue_word_nonblock(&targ->recv->queue, &rval))
            ;
        assert(val == rval);

        val++;
    }

    sk_m_print(&send);

    // second barrier is for the main thread to measure time
    pthread_barrier_wait(targ->tbar);

    return NULL;
}

static void *
dst_thr(void *arg)
{
    measure_receive = true;
    sk_m_init(&receive, NUM_REPETITIONS, "receive", m_buf_receive);

    struct thr_arg *targ = arg;

    aff_set_oncpu(targ->core_id);
    tsc_init(&targ->timer);

    // first barrier is for initialization
    pthread_barrier_wait(targ->tbar);

    uintptr_t val=0;
    bool __attribute__((unused)) ret;
    for (unsigned i=0; i<targ->count; i++) {

        // receive
        uintptr_t rval;
        for (;;) {

            ret = ump_dequeue_word_nonblock(&targ->recv->queue, &rval);

            if (ret) {
                break;
            }
        }
        assert(val == rval);

        //send
        ret = ump_enqueue_word_nonblock(&targ->send->queue, val);
        assert(ret == true); // only one message in-flight

        val++;
    }

    sk_m_print(&receive);

    // second barrier is for the main thread to measure time
    pthread_barrier_wait(targ->tbar);

    return NULL;
}

#define UMP_CONF_INIT(src_, dst_, shm_size_)      \
{                                                 \
    .src = {                                      \
         .core_id = src_,                         \
         .shm_size = shm_size_,                   \
         .shm_numa_node = numa_cpu_to_node(src_)  \
    },                                            \
    .dst = {                                      \
         .core_id = dst_,                         \
         .shm_size = shm_size_,                   \
         .shm_numa_node = numa_cpu_to_node(src_)  \
    },                                            \
    .shared_numa_node = -1,                       \
    .nonblock = 1                                 \
}

static void
do_pair_bench(int src, int dst)
{
    // Benchmarks send 1024 messages of one cache-line(?) = 64 Bytes each
    const int shm_size = (64*1024);
    const unsigned count = NUM_REPETITIONS;

    struct ump_bi_pair_state st;

    struct ump_pair_conf fwr_conf = UMP_CONF_INIT(src, dst, shm_size);
    struct ump_pair_conf rev_conf = UMP_CONF_INIT(dst, src, shm_size);

    st.fwr = ump_pair_state_create(&fwr_conf);
    st.rev = ump_pair_state_create(&rev_conf);

    pthread_t tids[2];
    struct thr_arg targs[2];
    pthread_barrier_t tbar;

    pthread_barrier_init(&tbar, NULL, 3);

    // forward direction
    targs[0].core_id = src;
    targs[0].send = &st.fwr->src;
    targs[0].recv = &st.rev->dst;
    targs[0].tbar = &tbar;
    targs[0].count = count;

    // reverse direction
    targs[1].core_id = dst;
    targs[1].send = &st.rev->src;
    targs[1].recv = &st.fwr->dst;
    targs[1].tbar = &tbar;
    targs[1].count = count;

    pthread_create(&tids[0], NULL, src_thr, &targs[0]);
    pthread_create(&tids[1], NULL, dst_thr, &targs[1]);

    tsc_t tsc; tsc_init(&tsc);
    // all threads initialize themselves and execute a barrier when ready
    pthread_barrier_wait(&tbar);
    tsc_start(&tsc);
    // all threads call a barrier before they finish
    pthread_barrier_wait(&tbar);
    tsc_pause(&tsc);

    pthread_join(tids[0], NULL);
    pthread_join(tids[1], NULL);

	tsc_report("total", &tsc);
	tsc_report("send", &targs[0].timer);
	tsc_report("recv", &targs[1].timer);
}

int main(int argc, const char *argv[])
{
    struct bench_conf bconf;

    if (argc < 3) {
        printf("Usage: %s <src> <dst>\n", argv[0]);
        printf(" src/dst: are core ranges (e.g., 1-10,20)\n");
        printf(" Example: %s 1-4 1-4\n", argv[0]);
        printf(" i->i is ignored\n");
        exit(0);
    }

    bconf.src = parse_ints_range(argv[1], &bconf.src_size);
    bconf.dst = parse_ints_range(argv[2], &bconf.dst_size);

    int src, dst;
    for (unsigned si=0; si<bconf.src_size; si++)
        for (unsigned di=0; di<bconf.dst_size; di++)
            if ( (src = bconf.src[si]) != (dst = bconf.dst[di])) {
                printf("%d -> %d\n", src, dst);
                do_pair_bench(src, dst);
            }

    return 0;
}
