/**
 * \file
 * \brief chain replication
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
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <smlt.h>
#include <smlt_message.h>

#include "incremental_stats.h"
#include "consensus.h"
#include "chain_replica.h"
#include "internal_com_layer.h"
#include "client.h"


#define CHAIN_COMMIT 4

/*
 * Message Layout when there is an array of uintptr_t
 * If the array is msg then 
 * msg[0] = tag | client_id | request_id
 * msg[4]- msg[6] payload
 */

typedef struct replica_t{	
    uint8_t id;
    uint8_t current_core;
    uint8_t num_clients;
    
    uint8_t num_replicas;
    uint64_t num_requests;	
    void (*exec_fn)(void *);

    // composition
    uint8_t level;
    uint8_t alg_below;
    uint8_t node_size;
    uint8_t started_from;
    uint8_t *cores;

    // communication
    uint8_t* clients;
    uint8_t* replicas;
 
    // specific for chain replication
    uint8_t rep_left;
    uint8_t rep_right;
    bool is_tail;
} replica_t;

static __thread replica_t replica;

static void update_value(void* cmd);
static void handle_request(struct smlt_msg* msg);

static void handle_setup(struct smlt_msg* msg);
static void handle_commit(struct smlt_msg* msg);


#ifdef MEASURE_TP
static bool timer_started = false;
static uint64_t total_start;
static uint64_t total_end;

static uint64_t num_reqs = 0;
static uint8_t runs = 0;
static double run_res[7];
static incr_stats avg;

static void print_results_chain(replica_t* rep) {

    init_stats(&avg);
    char* f_name = (char*) malloc(sizeof(char)*100);
#ifdef SMLT
    sprintf(f_name, "results/tp_%s_chain_num_%d_numc_%d", "adaptivetree", 
            rep->num_replicas, rep->num_clients);
#else
    sprintf(f_name, "results/tp_chain_below_%d_num_%d_numc_%d", 
            rep->alg_below, rep->num_replicas, rep->num_clients);
#endif
#ifndef BARRELFISH
    FILE* f = fopen(f_name, "a");
#endif
    RESULT_PRINTF(f, "#####################################################");
    RESULT_PRINTF(f, "#####################\n");
    RESULT_PRINTF(f, "algo_below %d num_clients %d topo %s \n", rep->alg_below, 
            rep->num_clients, "adaptivetree");
    for (int i = 2; i < 6; i++) {
        RESULT_PRINTF(f, "%10.3f \n", run_res[i]);
        add(&avg, run_res[i]);
    }
        
    RESULT_PRINTF(f, "avg %10.3f, stdv %10.3f, 95%% conf %10.3f\n", 
            get_avg(&avg), get_std_dev(&avg), get_conf_interval(&avg));

    RESULT_PRINTF(f, "||\t%10.3f\t%10.3f\t%10.3f\n", 
            get_avg(&avg), get_std_dev(&avg), get_conf_interval(&avg));

#ifndef BARRELFISH
    fflush(f);
    fclose(f);
#endif
}

static void* results_chain(void* arg)
{
    replica_t* rep = (replica_t*) arg;
    while (true){
        total_end = rdtsc();
        //double time = (double) (((double)total_end - total_start)/(CLOCKS_PER_SEC));
#ifdef BARRELFISH
        printf("Replica %d : Throughput/s current %10.6g \n",
                        disp_get_core_id(), (double) num_reqs/20);
#else
        printf("Replica %d : Throughput/s current %10.6g \n",
                sched_getcpu(), (double) num_reqs/20);
#endif
        run_res[runs] = (double) num_reqs/20;
        // reset stats
        num_reqs = 0;
        total_start = rdtsc();
        runs++;
        if (runs > 6){
            print_results_chain(rep);
            break;
        }
        sleep(20);
    }   
    return 0;
}
#endif

static void message_handler_chain(struct smlt_msg* msg) 
{
    switch (get_tag(msg->data)) {
        case SETUP_TAG:
            handle_setup(msg);
            break;
        case REQ_TAG:
            handle_request(msg);
            break; 
        case CHAIN_COMMIT:
            handle_commit(msg);
            break; 
        default:
            printf("unknown type in queue %lu \n", msg->data[0]);
    }
}

void message_handler_loop_chain(void) 
{
    errval_t err;
    struct smlt_msg* message = smlt_message_alloc(56);
    if (replica.id == 0) {
        int j = 0;
    
        while (true) {
            if (smlt_can_recv(replica.clients[j])) {
                err = smlt_recv(replica.clients[j], message);
                if (smlt_err_is_fail(err)) {
                    // TODO
                }
                message_handler_chain(message);
            }    
            j++;

            j = j % replica.num_clients;
        }

    } else {
       
        while (true) {
            if (smlt_can_recv(replica.replicas[replica.rep_left])) {
                err = smlt_recv(replica.replicas[replica.rep_left], message);
                if (smlt_err_is_fail(err)) {
                    // TODO
                }
                message_handler_chain(message);
            }    
        }
    }

}

/*
 * Handler functions for Message Passing
 */

static void handle_setup(struct smlt_msg* msg)
{
    errval_t err;
    uintptr_t core = get_client_id(msg->data);
    for (int i = 0; i < replica.num_clients; i++) {
        if (replica.clients[i] == core) {
            msg->data[4] = i;
        }
    }

    err = smlt_send(core, msg);
    if (smlt_err_is_fail(err)) {
        // TODO
    }
}

static void handle_request(struct smlt_msg* msg) 
{
    errval_t err;
#ifdef MEASURE_TP
    if (!timer_started) {
        total_start = rdtsc();
        timer_started = true;	
    }	
    num_reqs++;
#endif
    if (replica.id == 0) {
        set_tag(msg->data, CHAIN_COMMIT);
        // send to next
        err = smlt_send(replica.replicas[1], msg);
        if (smlt_err_is_fail(err)) {
            // TODO
        }

        if (replica.level == NODE_LEVEL) {

            // execute request
            if (replica.alg_below != ALG_NONE) {
                com_layer_core_send_request(msg);
            }
   
            update_value(&msg->data[4]);
        } else {
            update_value(&msg->data[4]);
            smlt_send(replica.started_from, msg);
            if (smlt_err_is_fail(err)) {
                // TODO
            }
        }
    } else {
        printf("Only leader should receive requests \n");
    }
}

static void handle_commit(struct smlt_msg* msg) 
{
    errval_t err;
    if (replica.id != 0) {
        if (!replica.is_tail) {
            err = smlt_send(replica.replicas[replica.rep_right], msg);
            if (smlt_err_is_fail(err)){
                // TODO
            }
        } else {
            set_tag(msg->data, RESP_TAG);
            err = smlt_send(replica.clients[get_client_id(msg->data)], msg);
            if (smlt_err_is_fail(err)) {
                // TODO
            }
        }
        // execute request
        if (replica.alg_below != ALG_NONE) {
            com_layer_core_send_request(msg);
        }
        update_value(&msg->data[4]);
    } else {
        printf("Replica %d: leader should not receive commit\n", replica.id);
        return;
    }

}

void set_execution_fn_chain(void (*exec_fn)(void *))
{
    replica.exec_fn = exec_fn;
}

static void default_exec_fn(void* addr);
static void default_exec_fn(void* addr)
{
    return;
}

void init_replica_chain(uint8_t id,
                            uint8_t current_core, 
                            uint8_t num_clients, 
                            uint8_t num_replicas, 
                            uint64_t num_requests,
                            uint8_t level, 
                            uint8_t alg_below, 
                            uint8_t node_size, 
                            uint8_t started_from, 
                            uint8_t* cores,
                            uint8_t* clients,
                            uint8_t* replicas,
                            void (*exec_fn)(void *))
{
    replica.id = id;
    replica.current_core = current_core;
    replica.num_clients = num_clients;
    replica.num_replicas = num_replicas;
    replica.alg_below = alg_below;
    replica.node_size = node_size;
    replica.started_from = started_from;
    replica.cores = cores;
    replica.clients = clients;
    replica.replicas = replicas;
    replica.level = level;
    if (exec_fn == NULL) {
        replica.exec_fn = &default_exec_fn;
    } else { 
        replica.exec_fn = exec_fn;
    }

    if (replica.alg_below != ALG_NONE) {
        com_layer_core_init(replica.alg_below, 
                            replica.id, 
                            replica.current_core,
                            replica.cores, 
                            replica.node_size, 
                            24,
                            replica.exec_fn);
    }

    if (replica.id == (replica.num_replicas-1)){
        replica.is_tail = true;
    }
 
    replica.rep_left = replica.id-1;
    replica.rep_right = replica.id+1;
    
    // connect to replicas
    if (id == 0) {
#ifdef MEASURE_TP
        pthread_t tid;
        pthread_create(&tid, NULL, results_chain, &replica);
#endif
    }
}

static void update_value(void* cmd)
{
    replica.exec_fn(cmd);
    return;
}

