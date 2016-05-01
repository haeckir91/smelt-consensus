/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012, 2013 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

/*
 * \brief Code for the management process.
 *
 * The management process is idle except for when messages are send to it.
 */

#include <stdio.h>

#include "barrelfish_compat.h"
#include "quorum.h"
#include "pthread.h"
#include "ump_conf.h"
#include "lxaffnuma.h"

// In main.c
extern int sending_node;
extern char app_name[];
extern coreid_t num_cores;

extern pthread_barrier_t barrier_init_done;
extern pthread_barrier_t barrier_exp_ab_done;

void* thr(void *arg);

int mgmt_loop(void* st)
{
    // Spawn domains
    QDBG("Spawning program on all cores\n");

    pthread_t *tids = malloc(get_num_cores()*sizeof(pthread_t));
    struct thr_arg *targs = malloc(get_num_cores()*sizeof(struct thr_arg));


    pthread_barrier_init(&barrier_init_done, NULL, num_cores);
    pthread_barrier_init(&barrier_exp_ab_done, NULL, num_cores);

    // Spawn program on all cores (one at a time)
    for (int i=0; i<num_cores; i++) {

        targs[i].core_id = i;
        pthread_create(&tids[i], NULL, thr, &(targs[i]));
    }

    // Wait for threads to terminate
    for (int i=0; i<num_cores; i++) {

        pthread_join(tids[i], NULL);
    }

    return 0;
}
