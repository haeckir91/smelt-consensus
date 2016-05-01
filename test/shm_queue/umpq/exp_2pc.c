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
#include "parse_int.h"

#define EXP_MAX 10000
 
extern struct process *processes;
extern __thread coreid_t this_core;
extern __thread struct node **nodes;

__thread int last_commited_version = 0;
__thread int last_version = 0;

__thread coreid_t num_neighbors = 0;

#include "doublylinkedlist.h"

__thread clist_t *active_reqs;

union d2pc {

    struct {
        uint8_t type;
        uint8_t node;
        uint32_t payload;
    } field;

    uintptr_t raw;
};

/*!
 * \brief Request a new version using 2PC
 */
void exp_2pc_req(void)
{
    last_version++;
}

/*!
 * \brief Forward message
 *
 * Forward message to all but \a node.
 *
 * \param node Neigbor to omit sending message to or -1 if message should 
 *    be send to everyone
 */
void exp_2pc_forward(uintptr_t val, coreid_t node)
{
    // Send to all children
    struct node* n = processes[this_core].children;
    while (n!=NULL) {

        if (n->node_idx!=node) {

            mp_send(n, val);
        }

        n = n->next;
    }

    // Send to parent
    n = processes[this_core].parent;
    if (n!=NULL && n->node_idx!=node) {

        mp_send(n, val);
    }
}

#define COMMIT_REQ 1
#define COMMIT_REQ_REP_YES 2
#define COMMIT_REQ_REP_NO 3
#define COMMIT 4
#define COMMIT_ACK 5

void tpc_vote(bool vote, coreid_t node, uint32_t version)
{
    union d2pc d;
    d.field.node = node;
    d.field.payload = version;
    d.field.type = COMMIT_REQ_REP_NO;
    
    vote &= version>last_version;

    if (vote) {
        d.field.type = COMMIT_REQ_REP_YES;
        last_version = version;
    }

    QDBG("tpc_vote: on core %d for %d [VOTE=%d]\n", 
         this_core, version, vote);

    // Vote yes if we did not request this version ourselfes
    mp_send(nodes[model_next_hop(d.field.node)], d.raw);
}

void tpc_forward_commit_req(coreid_t node, uint32_t payload)
{
    union d2pc d;
    d.field.type = COMMIT_REQ;
    d.field.node = node;
    d.field.payload = payload;

    // Foward to everyone else
    exp_2pc_forward(d.raw, model_next_hop(node));
        
    // Generate state
    // Make sure we did not create state for this before
    assert (NULL == clistFindNode(active_reqs, 
                                  node,
                                  payload));
    struct state2pc *state = init_state(node, payload);
    clistAddNode(active_reqs, state);
}

/*!
 * \brief Process incoming message
 *
 * Routing table?
 *
 * 1) COMMIT_REQ from NODE:
 *    forward to all neighbors except next_hop(NODE)
 *
 * 2) COMMIT_REQ_REP to NODE:
 *    send to NODE
 *    if NO -> send NO to "root"
 *    if YES -> send (YES & commit_okay())
 *
 * 3) COMMIT from NODE:
 *    Send to all neighbors except next_hop(NODE)
 *
 * \return Indicate whether polling should continue
 */
bool exp_2pc_process(uintptr_t val)
{
    union d2pc data = (union d2pc) val;

    if (data.field.type == COMMIT_REQ) {

        QDBG("COMMIT_REQ received on %d (neighbors=%d)\n", 
             this_core, num_neighbors);

        if (num_neighbors==1) {

            tpc_vote(true, data.field.node, data.field.payload);

        } else {

            tpc_forward_commit_req(data.field.node, data.field.payload);
        }
    } else if (data.field.type == COMMIT_REQ_REP_YES || 
               data.field.type == COMMIT_REQ_REP_NO) {

        // Retrieve state
        struct __cnode *n = clistFindNode(active_reqs, data.field.node,
                                          data.field.payload);
        assert (n!=NULL);

        struct state2pc *state = n->data;
        assert (state!=NULL);
        
        state->num_resp++;
        state->accept &= (data.field.type == COMMIT_REQ_REP_YES);

        QDBG("COMMIT_REQ_{YES,NO} received on core %d [%d out of %d]\n",
             this_core, state->num_resp, num_neighbors-1);

        if (data.field.node == this_core && state->num_resp>=num_neighbors) {
            // Abort polling, as we are on the root node
            return false;
        } else if(data.field.node != this_core && state->num_resp>=num_neighbors-1) {
            // Set answer to requester
            tpc_vote(state->accept, data.field.node, data.field.payload);
        }

    } else if (data.field.type == COMMIT) {

        assert(last_version>=data.field.payload); // Because we acknowledged the request
        assert(data.field.payload>last_commited_version);

        QDBG("COMMIT received, commiting %d from %d\n", 
             data.field.payload, 
             data.field.node);

        last_commited_version = data.field.payload;

        // Retrieve state
        struct __cnode *n = clistFindNode(active_reqs, data.field.node,
                                          data.field.payload);
        assert(n!=NULL || num_neighbors==1);
        if (n!=NULL) {
            free(n->data);
            clistDeleteNode(active_reqs, &n);

            // Foward to everyone else
            exp_2pc_forward(val, model_next_hop(data.field.node));
        }

    } else {
        assert(!"Shit");
    }

    return true;
}

void exp_2pc_recv(void)
{
    struct node *n = processes[this_core].parent;
    if (n==NULL) {
        n = processes[this_core].children;
    }
    
    while (last_commited_version<NUM_REPETITIONS) {

        assert(n!=NULL);

        uintptr_t val;
        bool ret = ump_dequeue_word_nonblock(&n->recv->queue, &val);

        if (ret) {
            if (!exp_2pc_process(val)) {
                return;
            }
        }

        if (n==processes[this_core].parent && processes[this_core].children!=NULL) {
            n = processes[this_core].children;
        } else {
            n = n->next;
            if (n==NULL) {
                n = processes[this_core].parent;
                if (n==NULL) {
                    n = processes[this_core].children;
                }
            }
        }
    }
}

static __thread struct sk_measurement skm_2pc;
static __thread uint64_t *buf = NULL;

void exp_2pc_phase1(uint32_t version)
{
    tpc_forward_commit_req(this_core, version);
}

void exp_2pc_phase2(uint32_t version)
{
    union d2pc d;
    d.field.type = COMMIT;
    d.field.node = this_core;
    d.field.payload = version;

    exp_2pc_forward(d.raw, -1);

}

extern int main_argc;
extern char **main_argv;

int exp_2pc(void)
{
    if (main_argc<2) {

        if (this_core==SEQUENTIALIZER)
            printf("Usage: %s <reqesters core range>\n", main_argv[0]);
        return 1;
    }

    active_reqs = clistCreateList();
    num_neighbors = model_num_neighbors(this_core);

    int *requesters;
    unsigned tuple_size;

    bool requester = false;
    requesters = parse_ints_range(main_argv[1], &tuple_size);

    for (int i=0; i<tuple_size; i++) 
        if (this_core == requesters[i])
            requester = true;

    free(requesters);

    printf("%3d is requester? %d\n", this_core, requester);

    if (this_core == SEQUENTIALIZER)
        printf("Requests from %d\n", requester);

    assert (sizeof(union d2pc)==sizeof(uintptr_t));

    if (requester) {
        buf = malloc(NUM_REPETITIONS*sizeof(uint64_t));
        sk_m_init(&skm_2pc, NUM_REPETITIONS, "2pc", buf);
    }

    for (int round=1; round<=NUM_REPETITIONS; round++) {

        if (requester) {

            sk_m_restart_tsc(&skm_2pc);

            exp_2pc_phase1(round);
            exp_2pc_recv();
            exp_2pc_phase2(round);

            sk_m_add(&skm_2pc);

            // Retrieve state
            struct __cnode *n = clistFindNode(active_reqs, this_core, round);
            assert(n!=NULL);
            free(n->data);
            clistDeleteNode(active_reqs, &n);

        } else {

            exp_2pc_recv();
        }

    }

    if (requester) {

        sk_m_print(&skm_2pc);
    }


    QDBG("2pc: terminating on core %d\n", this_core);

    return 0;
}
