/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012, 2013 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include "barrelfish_compat.h"

#include "quorum.h"
#include "model_defs.h"
#include "measurement_framework.h"

#include "pthread.h"

pthread_barrier_t pbarrier;
extern __thread coreid_t this_core;
__thread int num_barriers;

void mp_barrier(void)
{
    mp_reduction();
    mp_broadcast(SEQUENTIALIZER);
}

volatile uint32_t c1 = 0;
volatile uint32_t c2 = 0;

volatile uint64_t c_shm = 0;

void shm_barrier(void)
{
    static uint32_t round = 0;

    __sync_fetch_and_add(&c_shm, 1);
    while (c_shm<round*num_barriers) ;

    round++;
}

int exp_barrier(void)
{
    num_barriers = get_num_cores();

    struct sk_measurement m;
    uint64_t buf[NUM_REPETITIONS];

    if (this_core==SEQUENTIALIZER) {
        pthread_barrier_init(&pbarrier, NULL, num_barriers);
    }

    // --------------------------------------------------
    // own barrier implementation
    sk_m_init(&m, NUM_REPETITIONS, "barrier", buf);

    for (int i=0; i<NUM_REPETITIONS; i++) {

        sk_m_restart_tsc(&m);
        
        mp_barrier();

        sk_m_add(&m);
    }

    sk_m_print(&m);

#if 0
    // --------------------------------------------------
    // posix
    sk_m_init(&m, NUM_REPETITIONS, "posix", buf);

    for (int i=0; i<NUM_REPETITIONS; i++) {

        sk_m_restart_tsc(&m);

        pthread_barrier_wait(&pbarrier);

        sk_m_add(&m);
    }

    sk_m_print(&m);

    // --------------------------------------------------
    // shared data structure
    sk_m_init(&m, NUM_REPETITIONS, "shared", buf);

    for (int i=0; i<NUM_REPETITIONS; i++) {

        sk_m_restart_tsc(&m);

        __sync_fetch_and_add(&c1, 1);
        while (c1<i*num_barriers) ;

        /* if (this_core == SEQUENTIALIZER) { */
        /*     assert (c1==num_barriers); */
        /*     c1 = 0; */
        /* } */

        /* // ---------- */
        /* d = __sync_fetch_and_add(&c2, 1); */
        /* printf("%d,2: core %d incrementing from value %d\n",  */
        /*        i, this_core, d); */
        /* while (c2<num_barriers) ; */

        /* if (this_core == SEQUENTIALIZER) { */
        /*     assert (c2==num_barriers); */
        /*     c2 = 0; */
        /* } */

        /* printf("%2i: core %d reached end of shared loop\n",  */
        /*        i, this_core); */

        sk_m_add(&m);
    }

    sk_m_print(&m);
#endif
    return 0;
}
