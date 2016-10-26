/**
 * \file
 * \brief Replica implementation
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

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>

#include "consensus.h"
#include "tpc_replica.h"
#include "one_replica.h"
#include "broadcast_replica.h"
#include "chain_replica.h"
#include "raft_replica.h"
#include "shm_queue.h"

static __thread void (*exec_func)(void *);
static __thread uint8_t algorithm;
static __thread uint8_t lvl;
static __thread uint8_t id_d;
static __thread void (*msg_handler_loop_func)(void);

void set_execution_fn(void (*exec_fn)(void *))
{
    exec_func = exec_fn;
    switch (algorithm) {
        case ALG_TPC:
            set_execution_fn_tpc(exec_fn);
            break;
        case ALG_1PAXOS:
            set_execution_fn_onepaxos(exec_fn);
            break;

        case ALG_BROAD:
            set_execution_fn_broadcast(exec_fn);
            break;
        case ALG_CHAIN:
            set_execution_fn_chain(exec_fn);
            break;
        case ALG_RAFT:
            set_execution_fn_raft(exec_fn);
            break;
        case ALG_SHM:
            set_execution_fn_shm(exec_fn);
            break;
        default:
            printf("init_replica: unknown protocol \n");

    }
}

void* init_replica(void* arg)
{
    cons_args_t* rep_args = (cons_args_t*) arg;
    exec_func = rep_args->exec_func;
    algorithm = rep_args->algo;
    lvl = rep_args->level;
    id_d = rep_args->id;

    switch (algorithm) {
        case ALG_TPC:
            init_replica_tpc(rep_args->id, rep_args->current_core,
                    rep_args->num_clients, rep_args->num_replicas, 0, 
                    rep_args->level, rep_args->alg_below, rep_args->node_size, 
                    rep_args->started_from, rep_args->cores, rep_args->clients,
                    rep_args->replicas, rep_args->exec_func);	

            msg_handler_loop_func = &message_handler_loop_tpc;
            break;
        case ALG_1PAXOS:
            init_replica_onepaxos(rep_args->id, rep_args->current_core,
                    rep_args->num_clients, rep_args->num_replicas, 0, 
                    rep_args->level, rep_args->alg_below, rep_args->node_size, 
                    rep_args->started_from, rep_args->cores, rep_args->clients,
                    rep_args->replicas, rep_args->exec_func);
            msg_handler_loop_func = &message_handler_loop_onepaxos;	
            break;

        case ALG_BROAD:
            init_replica_broadcast(rep_args->id, rep_args->current_core,
                    rep_args->num_clients, rep_args->num_replicas, 0, 
                    rep_args->level, rep_args->alg_below, rep_args->node_size, 
                    rep_args->started_from, rep_args->cores, rep_args->clients,
                    rep_args->replicas, rep_args->exec_func);
            msg_handler_loop_func = &message_handler_loop_broadcast;	
            break;
        case ALG_CHAIN:
            init_replica_chain(rep_args->id, rep_args->current_core,
                    rep_args->num_clients, rep_args->num_replicas, 0, 
                    rep_args->level, rep_args->alg_below, rep_args->node_size, 
                    rep_args->started_from, rep_args->cores, rep_args->clients,
                    rep_args->replicas, rep_args->exec_func);
            msg_handler_loop_func = &message_handler_loop_chain;	
            break;
        case ALG_RAFT:
            init_replica_raft(rep_args->id, rep_args->current_core,
                    rep_args->num_clients, rep_args->num_replicas, 0, 
                    rep_args->level, rep_args->alg_below, rep_args->node_size, 
                    rep_args->started_from, rep_args->cores, rep_args->clients,
                    rep_args->replicas, rep_args->exec_func);
            msg_handler_loop_func = &message_handler_loop_raft;	
            break;
        case ALG_SHM:
            // TODO remove hardcoded cmd size
            if (lvl == NODE_LEVEL) {
                if (rep_args->id == 0) {
                    init_shm_writer(0, rep_args->current_core, 
                                    rep_args->num_clients, rep_args->num_replicas, 
                                    24, true, rep_args->shared_mem, rep_args->exec_func);	
                } else {
                    init_shm_reader(id_d, rep_args->current_core, 
                                    rep_args->num_replicas, 24, true, 
                                    0, rep_args->shared_mem, rep_args->exec_func);	
                }
            } else {
                init_shm_reader(id_d, rep_args->current_core,
                                rep_args->num_replicas, 24, false, rep_args->started_from,
                                rep_args->shared_mem, rep_args->exec_func);	
            }
           
            break;
        default:
            printf("init_replica: unknown protocol %d \n", algorithm);

    }

#ifdef BARRELFISH
    coreid_t core = disp_get_core_id();
#else
    uint32_t core = sched_getcpu();
#endif
    if (lvl == NODE_LEVEL) {
        printf("Node Replica on core %d: ready \n", core);
	} else {
        printf("Replica on core %d: ready \n", core);
    }
    start_handler_loop();
    return 0;
}

void start_handler_loop(void) 
{
    if (algorithm == ALG_SHM) {
        if (lvl == NODE_LEVEL) {
            if (id_d == 0) {	
                msg_handler_loop_func();
            } else {
                poll_and_execute();	
            }
        } else {
            poll_and_execute();	
        }
    } else {
        msg_handler_loop_func();
    }
}
