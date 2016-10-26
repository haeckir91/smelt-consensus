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
#include <string.h>

#include "ump_queue.h"
#include "ump_conf.h"

extern struct process *processes;
extern __thread coreid_t this_core;

static inline void mp_send_parent(uintptr_t payload)
{
    struct node* parent = processes[this_core].parent;
    assert(parent!=NULL);
    mp_send(parent, payload);
}

void mp_reduction(void)
{
    if (!model_is_in_group(this_core)) {

        QDBG("reduction: [membership] core %d is not in multicast group\n", 
             this_core);
    }
    else if (model_is_leaf(this_core)) {

        QDBG("reduction: [membership] core %d is leaf\n", this_core);

        // Send a message to parent
        mp_send_parent(this_core);
    }
    else {

        QDBG("reduction: [membership] core %d is _not_ leaf\n", this_core);

        uintptr_t sum = 0;

        int num_child = model_num_children(this_core);
        int num_recv = 0;
        int idx = 0;

        bool *received = malloc(num_child*sizeof(bool));
        memset(received, 0, num_child*sizeof(bool));
        
        struct node *child = processes[this_core].children;
        while (child!=NULL) {

            // Node should not be in child list of it is 
            // not participating in group communication
            assert(model_is_in_group(child->node_idx));

            if (!received[idx]) {

                struct ump_queue *q = &child->recv->queue;
                uintptr_t payload;

                bool r = ump_dequeue_word_nonblock(q, &payload);
                if (r) {
                    // Receive from child was successful
                    sum += payload;
                    received[idx] = true;
                    num_recv++;
                }
            }
            // Move pointer to next child
            child = child->next;
            idx ++;

            if (child==NULL && num_recv<num_child) {
                // We did not receive all messages, wrap around
                idx = 0;
                child = processes[this_core].children;
            } 
        }

        free(received);

        QDBG("reduction: core %d received all messages, "
             "forwarding to parent\n", this_core);

        sum += this_core;

        if (this_core != SEQUENTIALIZER) {
            mp_send_parent(sum);
        } else {
            QDBG("reduction: sequentializer %d received payload %"PRIuPTR"\n",
                 this_core, sum);
        }
    }

    QDBG("reduction: terminating on core %d\n", this_core);
}

void mp_broadcast(coreid_t c)
{
    if (this_core==c) {
        assert(model_is_in_group(this_core));
        mp_send_children(this_core);
    } else {
        if (model_is_in_group(this_core)) {
            mp_dispatch();
        }
    }
}

static __thread struct sk_measurement skm_red;
static __thread uint64_t *buf = NULL;

#define NUM_BENCH 1000000

int exp_reduction(void)
{
    double res = 0;

    for (int i=0; i<NUM_BENCH; i++) {
        uint64_t start = get_ticks();
        shm_barrier();
        res += (get_ticks()-start)*1.0/NUM_BENCH;
    }

    int sub = (int) res;

    printf("%2d cost of barrier is %5d\n", this_core, sub);

    buf = (uint64_t*) malloc(sizeof(uint64_t)*NUM_REPETITIONS);
    assert (buf);

    sk_m_init(&skm_red, NUM_REPETITIONS, "reduction", buf);

    for (int i=0; i<NUM_REPETITIONS; i++) {

        sk_m_restart_tsc(&skm_red);

        QDBG("reduction: broadcast done, starting reduction on %d\n", this_core);
        mp_reduction();

        // On the root, the cost of this barrier will be close to 0
        // because all the other cores already incremented the counter 
        // at this point
        shm_barrier();

        sk_m_add(&skm_red);
    }

    sk_m_print(&skm_red);
    free(buf);

    return 0;
}
