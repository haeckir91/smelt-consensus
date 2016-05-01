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
#include "shm/shm_queue.h"
#include "common/incremental_stats.h"

#define SHM_SIZE 4096
#define NUM_WRITES 1000000

int num_readers = 6;
int sleep_time = 0;
void* shared_mem;

typedef struct cmd_t {
    uint64_t val1;
    uint64_t val2;
    uint64_t val3; 
}cmd_t;

void exec_fn(void* arg)
{
    return;   
}


void* thr_writer(void* arg)
{
    cpu_set_t cpu_mask;

    CPU_ZERO(&cpu_mask);
    CPU_SET(0, &cpu_mask);

    sched_setaffinity(0, sizeof(cpu_set_t), &cpu_mask);

    init_shm_writer(0,
                    (uint64_t) arg,
                    0,
                    num_readers,
                    sizeof(cmd_t),
                    false,
                    //shared_mem,
                    NULL,
                    exec_fn);
    
    uint64_t rid = 1;
    cmd_t* cmd = malloc(sizeof(cmd_t));
    cmd->val1 = rid;
    cmd->val2 = rid;
    cmd->val3 = rid;

    uint64_t start;
    uint64_t end;
    uint64_t total;
    printf("###################################################\n");
    printf("Testing %d writes/reads \n", NUM_WRITES);
    printf("###################################################\n");
    while (true) {
         start = rdtsc();
         shm_write(cmd);
         end = rdtsc();
         total += end-start;
         rid++;
         cmd->val1 = rid;
         cmd->val2 = rid;
         cmd->val3 = rid;
         sleep(sleep_time);
         if (rid == (NUM_WRITES+1)) {
            break;
         }
    }

    while(true){};
    return 0;
}

void* thr_reader(void* arg)
{
    cpu_set_t cpu_mask;

    CPU_ZERO(&cpu_mask);
    CPU_SET((uint64_t) arg, &cpu_mask);

    sched_setaffinity(0, sizeof(cpu_set_t), &cpu_mask);
  
    init_shm_reader((((uint64_t) arg)-1),
                    (uint64_t) arg,
                    num_readers,
                    sizeof(cmd_t),
                    false,
                    0,
                    //shared_mem,
                    NULL,
                    exec_fn);

    uint64_t previous = 0;
    uint64_t result;
    uint64_t num_wrong = 0;
    while(true) {
        void* cmd = NULL;
        while (cmd == NULL) {
            cmd = shm_read();
            if (cmd == NULL) {
                // thread_yield();
            } else {
                result = ((cmd_t*) cmd)->val3;
                if (result == (previous+1)) {
                    previous++;
                    if (result == NUM_WRITES) {
                        goto exit;
                    }
                } else {
                    num_wrong++;
                }
            }
        }
    }
    
exit:
    printf("###################################################\n");
    if (num_wrong) {
        printf("Reader %"PRIu64": Test Failed \n", ((uint64_t)arg)-1);
    } else {
        printf("Reader %"PRIu64": Test Succeeded \n", ((uint64_t)arg)-1);
    }
    printf("###################################################\n");
    return 0;
}

int main(int argc, char ** argv)
{
    shared_mem = malloc(SHM_SIZE);
    pthread_t *tids = malloc((num_readers+1)*sizeof(pthread_t));
    printf("SHM test started \n");
    for (uint64_t i = 0; i < num_readers; i++) {
        pthread_create(&tids[i], NULL, thr_reader, (void*) i+1);
    }
    printf("SHM readers started \n");
    sleep(3);
    pthread_create(&tids[num_readers], NULL, thr_writer, (void*) 0); 
    printf("SHM writer started \n");
    for (int i = 0; i < num_readers; i++) {
        pthread_join(tids[i], NULL);
    }
}

