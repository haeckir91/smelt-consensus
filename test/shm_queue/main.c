/**
 * \brief Testing Shared memory queue
 */

/*
 * Copyright (c) 2015, ETH Zurich and Mircosoft Corporation.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <numa.h>

#include "shm.h"
#include "incremental_stats.h"
#include "ump_conf.h"


//#define PINGPONG

//#define DEBUG
#define SHM_SIZE SHMQ_SIZE*64
#ifdef DEBUG
#define NUM_WRITES 40
#else
#define NUM_WRITES 10000000
#endif

#define LEAF 0

int sleep_time = 0;
void* shared_mem;
void* shared_mem2;
#ifdef PINGPONG
static const int num_readers = 1;
#else
static const int num_readers = 3;
#endif
static struct ump_pair_state con[num_readers+1][num_readers+1];

#define UMP_CONF_INIT(src_, dst_, shm_size_)      \
{                                                 \
    .src = {                                      \
         .core_id = src_,                         \
         .shm_size = shm_size_,                   \
         .shm_numa_node = 0  \
    },                                            \
    .dst = {                                      \
         .core_id = dst_,                         \
         .shm_size = shm_size_,                   \
         .shm_numa_node = 0  \
     },                                            \
    .shared_numa_node = -1,                       \
    .nonblock = 1                                 \
}

/**
 * \brief Setup a pair of UMP channels.
 *
 * This is borrowed from UMPQ's pt_bench_pairs_ump program.
 */
void setup_chanel(int src, int dst)
{

    const int shm_size = (4096);

    struct ump_pair_conf fwr_conf = UMP_CONF_INIT(src, dst, shm_size);
    struct ump_pair_conf rev_conf = UMP_CONF_INIT(dst, src, shm_size);

    con[src][dst] = *ump_pair_state_create(&fwr_conf);
    con[dst][src] = *ump_pair_state_create(&rev_conf);
}

void* thr_writer(void* arg)
{
    struct shm_queue* queue;
    cpu_set_t cpu_mask;
    incr_stats avg;
    init_stats(&avg);

    CPU_ZERO(&cpu_mask);
    CPU_SET(0, &cpu_mask);

    sched_setaffinity(0, sizeof(cpu_set_t), &cpu_mask);

    queue = shm_init_context(shared_mem,
                     num_readers,
                     0);
#ifdef PINGPONG   
    struct shm_queue* queue2;
    queue2 = shm_init_context(shared_mem2,
                     num_readers,
                     0);
#endif
    uint64_t rid = 1;
    uint64_t start;
    uint64_t end;
    uintptr_t res[8];
    while (true) {
#ifdef DEBUG
        sleep(1);
#endif
        start = rdtsc();

        shm_send_raw(queue, rid, start, 0, 0, 0, 0, 0);
#ifdef PINGPONG
        shm_receive_raw(queue2, &res[0], &res[1], &res[2],
                        &res[3], &res[4], &res[5], &res[6]);
#else
        ump_dequeue(&con[LEAF][0].dst.queue, res);
#endif
        end = rdtsc();
        rid++;
        if (((end-start) < 500000) && rid > 100000) {
            add(&avg, (double) end -start);
        }

        if (rid == NUM_WRITES) {
            break;
        }
    }
    
    sleep(1);

    printf("###################################################\n");
    printf("Broadcast \nrt %10.3f \nstdv %10.3f \n",
            get_avg(&avg), get_std_dev(&avg));

    return 0;
}

void* thr_reader(void* arg)
{   
    if (((uint64_t) arg) == (LEAF)) {
       setup_chanel((LEAF), 0);
    }
    struct shm_queue* queue;
    cpu_set_t cpu_mask;

    CPU_ZERO(&cpu_mask);
    uint64_t core = (uint64_t) arg;
    core++;
    //core = core*4;
    CPU_SET(core, &cpu_mask);

    sched_setaffinity(0, sizeof(cpu_set_t), &cpu_mask);

    queue = shm_init_context(shared_mem,
                     num_readers,
                     (uint64_t) arg);
#ifdef PINGPONG
    struct shm_queue* queue2;
    queue2 = shm_init_context(shared_mem2,
                     num_readers,
                     (uint64_t) arg);
#endif

    printf("ID %ld CORE %ld \n", (uint64_t) arg, (uint64_t) core);
    uint64_t previous = 0;
    uint64_t num_wrong = 0;
    uintptr_t r[8];

    while(true) {
        shm_receive_raw(queue, &r[0], &r[1], &r[2], &r[3],
                        &r[4], &r[5], &r[6]);

        if (((uint64_t) arg) == (num_readers-1)) {
#ifdef PINGPONG
            shm_send_raw(queue2, 0, 0, 0, 0,
                        0, 0, 0);
#else
            ump_enqueue(&con[LEAF][0].src.queue, 0,
                        0,0,0,0,0,0);
#endif
        }
        if (r[0] != (previous+1)) {
            num_wrong++;  
        }

        previous++;
        
        if (previous == NUM_WRITES-1) {
            break;
        }
    }
    
    printf("###################################################\n");
    if (num_wrong) {
        printf("Reader %"PRIu64": Test Failed \n", ((uint64_t)arg));
    } else {
        printf("Reader %"PRIu64": Test Succeeded \n", ((uint64_t)arg));
    }
    printf("###################################################\n");
    return 0;
}

void* thr_ump(void* arg)
{
    uint64_t end, start;
    uintptr_t res[8];

    cpu_set_t cpu_mask;
    incr_stats avg;
    init_stats(&avg);

    incr_stats rt;
    init_stats(&rt);

    CPU_ZERO(&cpu_mask);
    CPU_SET((uint64_t) arg, &cpu_mask);
    //CPU_SET((uint64_t) arg*4, &cpu_mask);

    sched_setaffinity(0, sizeof(cpu_set_t), &cpu_mask);

    if (((uint64_t) arg ) == 0) {
        for (int i = 0; i < NUM_WRITES; i++) {
            start = rdtsc();
            for (int j = 1; j < (num_readers+1);j++) {
                ump_enqueue(&con[0][j].src.queue,
                            i, start, 0, 0, 0, 0, 0);
            }

            ump_dequeue(&con[num_readers][0].dst.queue,
                        res);
            end = rdtsc();
            if (i > 1000000) {
                if (end - start < 500000) {
                    add(&avg, (double) end - start);
                }
            }
        }

        sleep(2);

        printf("###################################################\n");
        printf("Broadcast \nrt %10.3f \nstdv %10.3f \n",
            get_avg(&avg), get_std_dev(&avg));
    } else {
        uint64_t j = (uint64_t) arg;
        for (int i = 0; i < NUM_WRITES; i++) {
                ump_dequeue(&con[0][j].dst.queue, res);
                if (((uint64_t)arg)== (num_readers)) {
                    ump_enqueue(&con[j][0].src.queue, 0,
                                0,0,0,0,0,0);
                }
        }
    }
    return 0;
}





int main(int argc, char ** argv)
{
    shared_mem = calloc(1,SHM_SIZE*64);
    shared_mem2 = calloc(1,SHM_SIZE*64);
    pthread_t *tids = (pthread_t*) malloc((num_readers+1)*sizeof(pthread_t));
    printf("###################################################\n");
    printf("SHM test started (%d writes/reads) \n", NUM_WRITES);
    printf("###################################################\n");
    for (uint64_t i = 0; i < (uint64_t) num_readers; i++) {
        pthread_create(&tids[i], NULL, thr_reader, (void*) i);
    }
    sleep(1);

    pthread_create(&tids[num_readers], NULL, thr_writer, (void*) 0); 
    for (uint64_t i = 0; i < (uint64_t) num_readers+1; i++) {
        pthread_join(tids[i], NULL);
    }


    for (int i = 1; i < num_readers+1; i++) {
        setup_chanel(0,i);
    }

    printf("###################################################\n");
    printf("UMQ test started \n");
    printf("###################################################\n");
    for (uint64_t i = 0; i < (uint64_t) num_readers+1; i++) {
        pthread_create(&tids[i], NULL, thr_ump, (void*) i);
    }

    for (uint64_t i = 0; i < (uint64_t) num_readers+1; i++) {
        pthread_join(tids[i], NULL);
    }

}

