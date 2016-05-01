/**
 * \file
 * \brief
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012, 2013 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef QUORUM_H
#define QUORUM_H

#include <stdbool.h>
#include "model_defs.h"
#include "barrelfish_compat.h"
#include <pthread.h>

// --------------------------------------------------
// CONFIGURATION

#define NUM_EXP 1
#define NUM_REPETITIONS 100
#define Q_MAX_CORES 48
#define Q_MAX_NODES 16
#define QRM_ROUND_MAX 1000
//#define RAW_UMP 1
#define SEQUENTIALIZER 0 // node that acts as the sequentializer
#define SHM_SIZE (4096*1024) // 4 KB = 2^12 = 2^6 * 2^6 (64 Byte) -> enough space for 64 messages
#define EXP_MP 1 // Enable message passing
//#define MEASURE_SEND_OVERHEAD 1 // Measure overhead of send_node_payload_raw
//#define EXP_SHM 1 // Enable shared memory
//#define EXP_HYBRID 1 // Enable hybrid broadcast
//#define QRM_DBG_ENABLED 1
#define INDIVIDUAL_MEASUREMENTS 1
#define UMP_BUFFER_SIZE (NUM_REPETITIONS*MODEL_NUM_CORES*64)
#define W_UNDETERMINISM 1
// --------------------------------------------------
// DATA STRUCTURES

/*
 * \brief Per-process state.
 *
 * Stores the UMP channels to children and parent, which are linked
 * lists of ump_peer_state structures.
 */
struct process {
    struct node *children;
    struct node *parent;
};

/*
 * \brief Element of children/parent link-list for channel information
 *
 */
struct node {
    struct ump_peer_state *send, *recv;
    struct node *next;
    int node_idx;
};

// --------------------------------------------------
// DEBUG

#ifdef QRM_DBG_ENABLED
#define QDBG(...) printf(__VA_ARGS__)
#else /* QRM_DBG_ENABLED */
#define QDBG(...) ;
#endif /* QRM_DBG_ENABLED */


// --------------------------------------------------
// FUNCTIONS

/* void q_request(struct quorum_binding *b, uint32_t id, uint32_t id2); */
/* void q_ack(struct quorum_binding *b, uint32_t id, uint32_t id2); */
/* void add_binding(struct quorum_binding *b, coreid_t core); */
/* void update_bindings(struct quorum_rx_vtbl *vtbl); */
/* void qrm_exp_round(void); */

int model_is_edge(coreid_t src, coreid_t dest);
int model_is_parent(coreid_t core, coreid_t parent);
int model_num_children(coreid_t core);
int model_num_neighbors(coreid_t core);
bool model_is_leaf(coreid_t core);
int model_has_edge(coreid_t dest);
int model_next_hop(coreid_t core);
int model_get_num_cores_in_group(void);
bool model_does_mp_send(coreid_t core);
bool model_does_mp_receive(coreid_t core);
bool model_does_shm_send(coreid_t core);
bool model_does_shm_receive(coreid_t core);
bool model_is_in_group(coreid_t c);
int model_get_mp_order(coreid_t core, coreid_t child);
void model_debug(coreid_t c1, coreid_t c2);

// managment connection (mgmt_connection)
void connect_to_mgmt_process(void);
/* void quorum_init_reply(struct quorum_mgmt_binding *b, struct capref sm, struct capref numa_sm); */


/* **************************************************
 * Message passing primitives
 * ************************************************** */
void      mp_send(struct node *n, uintptr_t val);
int       mp_send_children(uintptr_t payload);
void      mp_dispatch(void);
uintptr_t mp_receive(struct node *n);
void      mp_reduction(void);
void      mp_broadcast(coreid_t core);

/* **************************************************
 * Shared memory primitives
 * ************************************************** */
void      shm_barrier(void);

/* **************************************************
 * Experiments
 * ************************************************** */
int exp_ab(void);
int exp_reduction(void);
int exp_2pc(void);
int exp_barrier(void);

// management process (qrm_mgmt)
int mgmt_loop(void* st);

// shm
int exp_shm_fifo(void);

// hybrid
int exp_hybrid(void);

// tree
int tree_init(void);
void init_ump_pair(coreid_t src, coreid_t dst, 
                   struct node **ret_send, struct node **ret_receive);

struct thr_arg {
    unsigned              core_id;
    struct ump_peer_state *send, *recv;
    pthread_barrier_t     *tbar;
    unsigned              count;
};

#endif // QUORUM_H
