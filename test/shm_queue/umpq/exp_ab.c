/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012, 2013 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#define BATCH_SIZE 10

#include "barrelfish_compat.h"

#include "quorum.h"
#include "model.h"
#include "model_defs.h"
#include "measurement_framework.h"

#include "ump_queue.h"
#include "ump_chan.h"
#include "ump_conf.h"
#include "tsc.h"

#include <pthread.h>
#include <unistd.h>

extern coreid_t num_cores;
extern struct process *processes;
extern __thread coreid_t this_core;

pthread_barrier_t barrier_exp_ab_done;

// Thread local variables
// --------------------------------------------------
__thread int num_send = 0; // number of sends: 1 per message
static __thread int num_tx   = 0; // number of transmits: 1 per message * child
static __thread int num_rx   = 0; // number of receives


#if defined(INDIVIDUAL_MEASUREMENTS)
static __thread struct sk_measurement skm_rtt;
#endif /* INDIVIDUAL_MEASUREMENTS */


/*
 * \brief Low-level send function
 *
 * This is a wrapper for send_node_payload_raw. The arguments are
 * packed in a static variable and a pointer to it given to _raw.
 *
 * This is needed due to the send_continuation weirdness in case the
 * channel is busy. We can only use functions with an arg* as argument
 * for these.
 */
inline void mp_send(struct node *n, uintptr_t val)
{
    bool ret;
    
    assert(n!=NULL);
    assert(n->node_idx != this_core);

    QDBG("mp_send(#send=%d,#tx=%d): %d->%d(%p) (paylod=%"PRIuPTR")\n",
         num_send, num_tx+1, this_core, n->node_idx, 
         &n->send->queue, val);

    struct ump_chan *c = n->send->queue.chan;
    QDBG("channel state: next_id %d ack_id %d tx.bufmsgs %d\n",
         c->next_id, c->ack_id, c->tx.bufmsgs);
    assert (ump_chan_can_send(c));
    
    ret = ump_enqueue_word_nonblock(&n->send->queue, val);
    assert(ret==true);

    num_tx++;
}

/*
 * \brief Receive on given binding
 *
 * This function polls on the given queue for messages.
 */
inline uintptr_t mp_receive(struct node* n)
{
    bool ret;
    uintptr_t val;

    assert(n->recv != NULL);
    
    for (;;) {

        ret = ump_dequeue_word_nonblock(&n->recv->queue, &val);

        if (ret) {
            break;
        }
    }

    num_rx++;
    QDBG("mp_receive(#rx=%d): %d->%d (paylod=%"PRIuPTR")\n",
         num_rx, n->node_idx, this_core, val);

    return val;
}

/*
* \brief Send a message along the tree to all children
*
* Send a message to all children in the order given by walking qrm_children.
*/
inline int mp_send_children(uintptr_t payload)
{
    // Walk children and send a message each
    struct node* n = processes[this_core].children;
    while (n!=NULL) {

        mp_send(n, payload);

        num_send++;
        n = n->next;
    }

    return 0;
}

/*
 * \brief Process incoming message
 *
 * Function will trigger forwarding the message along the tree
 */
inline static void process_incoming_message(uintptr_t payload)
{
    if (payload==this_core) {
        // Received our own message back via tree
        QDBG("received OWN message back via ab channel\n");

        #if defined(INDIVIDUAL_MEASUREMENTS)
        if (num_rx%BATCH_SIZE==0) {
            sk_m_add(&skm_rtt);
        }
        #endif /* INDIVIDUAL_MEASUREMENTS */

    } 

    mp_send_children(payload);
}

/*
 * \brief Poll on incoming channels without waitsets
 *
 * For the sequentializer, this falls back on qrm_tree_dispatch, as a
 * message can potentially be received from every other node and it
 * would be a pain to get polling of serveral channels right without
 * using waitsets.
 */
inline void mp_dispatch(void)
{
    struct node* qrm_parent = processes[this_core].parent;

    if (qrm_parent==NULL) {
        printf("core %d doesn't have a parent\n", this_core);
    }
    assert(qrm_parent!=NULL);

    // Receive and process message
    uintptr_t payload = (uint32_t) mp_receive(qrm_parent);
    process_incoming_message(payload);
}

static void exp_send(struct node *dest)
{
    // sending node needs to be part of the multicast group
    assert (model_is_in_group(this_core));

    printf("Starting to send from core %d\n", this_core);

    #if defined(INDIVIDUAL_MEASUREMENTS)
    uint64_t buf[NUM_REPETITIONS];
    sk_m_init(&skm_rtt, NUM_REPETITIONS, "ab_rtt", buf);
    #endif /* INDIVIDUAL_MEASUREMENTS */

    uint64_t start = get_ticks();
    assert(NUM_REPETITIONS%BATCH_SIZE==0);
    for (int round=0; round<NUM_REPETITIONS/BATCH_SIZE; round++) {

        #if defined(INDIVIDUAL_MEASUREMENTS)
        sk_m_restart_tsc(&skm_rtt);
        #endif /* INDIVIDUAL_MEASUREMENTS */

        // Send a message
        for (int i=0; i<BATCH_SIZE; i++) {
            mp_send(dest, this_core);
            num_send++;
        }

        // Wait until we get the message back over the tree

        
        for (int i=0; i<BATCH_SIZE; i++) {
            // Measurement ends in process_incoming_message
            mp_dispatch();
        }
    }
    uint64_t diff = get_ticks()-start;
    printf("Sending done [%s] [%s], time=%"PRIu64" %lf\n",
           MACHINE, TOPOLOGY,
           diff, 1.*diff/NUM_REPETITIONS);

    #if defined(INDIVIDUAL_MEASUREMENTS)
    sk_m_print(&skm_rtt);
    #endif /* INDIVIDUAL_MEASUREMENTS */
}

static void exp_receive(void)
{
    assert(model_is_in_group(this_core));

    printf("Receiving on core %d from %d on %p\n", this_core, 
           processes[this_core].parent->node_idx,
           &processes[this_core].parent->recv->queue);

    for (int round=0; round<NUM_REPETITIONS; round++) {

        // Wait for messages
        mp_dispatch();
    }
}

/*
* \brief Atomic broadcast experiment
*
* Implementation uses an sequentializer to guarantee global order.
*
*/
int exp_ab(void)
{
    #define REP 1000000
    if (this_core==SEQUENTIALIZER) {
        uint64_t test = get_ticks();
        uint64_t tmp = 0;
        for (int i=0; i<REP; i++) {
            tmp += this_core;
        }
        uint64_t test2 = get_ticks() - test;
        printf("Dummy: %"PRIu64" in %lf\n", tmp, (test2*1./REP));
        
    }

    // Send from all nodes
    for (int i=0; i<num_cores; i++) {

        // Does not work for sequentializer
        if (i==SEQUENTIALIZER || !model_is_in_group(i))
            continue;

        // Barrier
        pthread_barrier_wait(&barrier_exp_ab_done);

        struct node *send = NULL;
        struct node *receive = NULL;

        // Initialize sender -> sequentializer
        if (this_core == i) {

            // This core is the sender
            init_ump_pair(i, SEQUENTIALIZER, &send, &receive);
            processes[SEQUENTIALIZER].parent = receive;

            printf("--------------------------------------------------\n");
        }
        
        // Barrier
        pthread_barrier_wait(&barrier_exp_ab_done);

        // Do nothing if sending core is not part of group OR
        // this process is not part of the group
        if (!model_is_in_group(this_core))
            continue;

        if (this_core == i) {

            assert (this_core!=SEQUENTIALIZER);
            sleep(1);

            QDBG("exp_ab: sending from node %d\n", i);

            assert (send!=NULL);
            exp_send(send);
        } 
        else {

            exp_receive();
        }
    }
    return 0;
}

