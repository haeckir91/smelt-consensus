/**
 * \file
 * \brief Interface for starting consensus service
 */

/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
 * Attn: Systems Group.
 */
#ifndef _consensus_h
#define _consensus_h 1

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_NUM_CLIENTS 64
#define MAX_NUM_REPLICAS 64

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

#define MEASURE_TP
#define KVS

#define CORE_LEVEL 0
#define NODE_LEVEL 1

#define ALG_1PAXOS 0
#define ALG_TPC 1
#define ALG_BROAD 2
#define ALG_CHAIN 3
#define ALG_RAFT 4
#define ALG_SHM 5
#define ALG_NONE 6

#define ALG_1PAXOS_ARG "0"
#define ALG_TPC_ARG "1"
#define ALG_BROAD_ARG "2"
#define ALG_CHAIN_ARG "3"
#define ALG_RAFT_ARG "4"
#define ALG_SHM_ARG "5"
#define ALG_NONE_ARG "6"

#ifdef BARRELFISH
#define RESULT_PRINTF(file, x...) debug_printf(x);
#else
#define RESULT_PRINTF(file, x...) fprintf(file, x);
#endif


// should we use libsyncs tree?
//#define LIBSYNC

// Struct given as an argument to start a replica
// used by pthreads_create
typedef struct cons_args_t{
    uint8_t id;
    uint8_t current_core;
    uint8_t num_clients;
    uint8_t num_replicas;
    uint8_t num_requests;
    uint8_t level;
    uint8_t alg_below;
    uint8_t algo;
    uint8_t node_size;
    uint8_t started_from;
    uint8_t* cores;
    void* shared_mem;
    uint8_t* clients;
    uint8_t* replicas;
    void (*exec_func)(void*);
} cons_args_t;


/**
 * \brief initializing algorithm on node level, core level will be started automatically
 *
 * \param total_cores number of cores the machine has
 * \param algorithm	which algorithm should be started on a subset of cores
 * \param cores		array of core numbers on which the algorithm should 
 *			be started
 * \param num_cores	length of the array cores
 * \param num_clients	the number of clients that connect to the protocol
 * \param alg_below	algorithm that should be started below
 * \param node_size	number of cores in a node
 * \param client_cores the cores on which clients are started
 * \param exec_func function that should be executed after agreement
 */
void consensus_init(uint8_t total_cores,
            uint8_t algorithm, 
		    uint8_t* cores,
		    uint8_t num_cores,
		    uint8_t num_clients,
		    uint8_t alg_below,
		    uint8_t node_size,
		    uint8_t* node_cores,
            uint8_t* client_cores,
            void (*exec_func)(void*));

/**
 * \brief initializing algorithm on node level, core level will be started automatically
 *
 * \param exec_func	function that should be executed after agreement
 * \param failures	Should the consensus protocol started tolerate failures?
 * \param full_replication	Should the replication be on all cores or only once per NUMA node?
 */
void consensus_init_auto(
		    void (*exec_func)(void* arg),
		    bool failures,
		    bool full_replication);
/**
 * \brief initializing benchmark clients on node level
 *
 * \param num_cores number of cores the machine has
 * \param cores		array of core numbers on which the clients should 
 *			be started
 * \param num_clients	length of the array cores
 * \param num_replicas	number of replicas
 * \param last_replica	need to know the core of the last replica (chain replication)
 * \param sleep_time	sleep time between requests
 * \param protocol      Only used when compiled with libsync
 * \param protocol_below      Only used when compiled with libsync
 * \param topo          If libsync is used the number of the tree topology
 * \param replica_cores          cores on which the replicas are running
 *                  
 */

void consensus_bench_clients_init(uint8_t num_cores,
            uint8_t* cores,
		    uint8_t num_clients,
            uint8_t num_replicas,
            uint8_t last_replica,
		    uint64_t sleep_time,
            uint8_t protocol,
            uint8_t protocol_below,
            uint8_t topo,
            uint8_t* replica_cores);


/*
 *  initalization of replicas and setting execution function
 */

/**
 * \brief initializing a replica accoriding to arguments in struct
 *        given as pointer argument
 *
 * \param arg a struct of type cons_args_t. this function is called by 
 *        pthreads to start a new replica
 */
void* init_replica(void* arg);

void set_execution_fn(void (*exec_fn)(void *));
void start_handler_loop(void);

#endif // _consensus_h

