#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <sched.h>
#include <pthread.h>

#include "ump_chan.h"
#include "ump_queue.h"
#include "ump_os.h"
#include "test_driver.h"

#include <sched.h>

static const char *g_appname;

// state private to source or sink
struct private_state {
    struct ump_chan chan;
    struct ump_queue queue;
};

static void *source_thread(void *param)
{
    struct private_state *st = param;
    test_source_thread(&st->queue);
    return NULL;
}

static void *sink_thread(void *param)
{
    struct private_state *st = param;
    test_sink_thread(&st->queue);
    return NULL;
}

static void usage(void)
{
    fprintf(stderr, "Usage: %s [option...] source_core sink_core\n", g_appname);
    exit(1);
}

static void die(void)
{
    exit(2);
}

static uint8_t parse_core_id_or_die(const char *str)
{
    unsigned long ret;
    char *endptr = NULL;

    ret = strtoul(str, &endptr, 0);
    if (endptr == NULL || *endptr != '\0' || ret > CHAR_MAX) {
        fprintf(stderr, "Failed to parse '%s' as core ID\n", str);
        usage();
    }

    return (uint8_t)ret;
}

typedef void *pthread_start_fn_t(void *);

static void  create_pinned_thread_or_die(pthread_t *tid,
                                         pthread_start_fn_t *func,
                                         void *param,
                                         int cpu)
{
    int err __attribute__((unused));
    pthread_attr_t attr;
    cpu_set_t cpu_mask;

    CPU_ZERO(&cpu_mask);
    CPU_SET(cpu, &cpu_mask);

    err = pthread_attr_init(&attr);
    assert(!err);

    err = pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpu_mask);
    assert(!err);

    err = pthread_create(tid, &attr, func, param);
    assert(!err);
}

int main(int argc, char *argv[])
{
    int i;
    pthread_t source_handle, sink_handle;
    void *tx_event, *rx_event;
    uint8_t source_core, sink_core;
    struct private_state *source_state, *sink_state;
    void *shmem;
    const size_t VM_SIZE = 4096 * 4;

    g_appname = argv[0];

    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            break;
        } else if (strcmp(argv[i], "-h") == 0) {
            usage();
        } else {
            fprintf(stderr, "Unknown option '%s'\n", argv[i]);
            usage();
        }
    }

    if (i + 2 != argc) {
        usage();
    }

    source_core = parse_core_id_or_die(argv[i]);
    sink_core = parse_core_id_or_die(argv[i + 1]);

    shmem = calloc(VM_SIZE, 1);
    assert(shmem != NULL);

    source_state = calloc(1, sizeof(struct private_state));
    assert(source_state != NULL);

    sink_state = calloc(1, sizeof(struct private_state));
    assert(sink_state != NULL);

    ump_wake_context_setup(&tx_event);
    ump_wake_context_setup(&rx_event);

    if (!ump_chan_init(&source_state->chan, shmem, VM_SIZE, true, rx_event)) {
        fprintf(stderr, "ump_chan_init failed for source\n");
        die();
    }

    if (!ump_chan_init(&sink_state->chan, shmem, VM_SIZE, false, tx_event)) {
        fprintf(stderr, "ump_chan_init failed for sink\n");
        die();
    }

    ump_queue_init(&source_state->queue, &source_state->chan, tx_event);
    ump_queue_init(&sink_state->queue, &sink_state->chan, rx_event);

    create_pinned_thread_or_die(&source_handle, source_thread, source_state, source_core);
    create_pinned_thread_or_die(&sink_handle, sink_thread, sink_state, sink_core);

    pthread_join(sink_handle, NULL);
    pthread_join(source_handle, NULL);

	return 0;
}
