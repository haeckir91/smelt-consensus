/**
 * \file
 * \brief broadcasting service for consensus
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
#include <time.h>

#include "crc.h"
#include "incremental_stats.h"
#include "consensus.h"
#include "broadcast_replica.h"
#include "internal_com_layer.h"
#include "client.h"
#include "mp.h"
#include "topo.h"


#define BROAD_COMMIT 4

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
 
} replica_t;

static __thread replica_t replica;

static void update_value(void* cmd);
static void handle_request(uintptr_t* msg);

static void handle_setup(uintptr_t* msg);
static void handle_commit(uintptr_t* msg);


#ifdef MEASURE_TP
static bool timer_started = false;
static uint64_t total_start;
static uint64_t total_end;

static uint64_t num_reqs = 0;
static uint8_t runs = 0;
static double run_res[7];
static incr_stats avg;

static void print_results_broad(replica_t* rep) {

    init_stats(&avg);
    char* f_name = (char*) malloc(sizeof(char)*100);
#ifdef LIBSYNC
    sprintf(f_name, "results/tp_%s_broad_num_%d_numc_%d", topo_get_name(), 
            rep->num_replicas, rep->num_clients);
#else
    sprintf(f_name, "results/tp_broad_below_%d_num_%d_numc_%d", 
            rep->alg_below, rep->num_replicas, rep->num_clients);
#endif
#ifndef BARRELFISH
    FILE* f = fopen(f_name, "a");
#endif
    RESULT_PRINTF(f, "#####################################################");
    RESULT_PRINTF(f, "#####################\n");
    RESULT_PRINTF(f, "algo_below %d num_clients %d topo %s \n", rep->alg_below, 
            rep->num_clients, topo_get_name());
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

static void* results(void* arg)
{
    replica_t* rep = (replica_t*) arg;
    while (true){
        total_end = rdtsc();
        //double time = (double) (((double)total_end - total_start)/(CLOCKS_PER_SEC));
#ifndef BARRELFISH
        printf("Replica %d : Throughput/s current %10.6g \n",
                sched_getcpu(), (double) num_reqs/20);
#else
        printf("Replica %d : Throughput/s current %10.6g \n",
                        disp_get_core_id(), (double) num_reqs/20);
#endif
        run_res[runs] = (double) num_reqs/20;
        // reset stats
        num_reqs = 0;
        total_start = rdtsc();
        runs++;
        if (runs > 6){
            print_results_broad(rep);
            break;
        }
        sleep(20);
    }   
    return 0;
}
#endif

static void message_handler_broadcast(uintptr_t* msg) 
{
    switch (get_tag(msg)) {
        case SETUP_TAG:
            handle_setup(msg);
            break;
        case REQ_TAG:
            handle_request(msg);
            break; 
        case BROAD_COMMIT:
            handle_commit(msg);
            break; 
        default:
            printf("unknown type in queue %lu \n", msg[0]);
    }
}

#ifdef LIBSYNC
void message_handler_loop_broadcast(void)
{

    uintptr_t* message = (uintptr_t*) malloc(sizeof(uintptr_t)*8);
    if (replica.id == 0) {
        int j = 0;
    
        while (true) {
            if (mp_can_receive(replica.clients[j])) {
                mp_receive7(replica.clients[j], message);
                message_handler_broadcast(message);
            }    
            j++;

            j = j % (replica.num_clients);
        }

    } else {
       
        while (true) {
            mp_receive_forward7(message);
            message_handler_broadcast(message);
        }
    }
}
#else
void message_handler_loop_broadcast(void) 
{
    uintptr_t* message = (uintptr_t*) malloc(sizeof(uintptr_t)*8);
    if (replica.id == 0) {
        int j = 0;
    
        while (true) {
            if (mp_can_receive(replica.clients[j])) {
                mp_receive7(replica.clients[j], message);
                message_handler_broadcast(message);
            }    
            j++;

            j = j % replica.num_clients;
        }

    } else {
       
        while (true) {
            if (mp_can_receive(replica.replicas[0])) {
                mp_receive7(replica.replicas[0], message);
                message_handler_broadcast(message);
            }    
        }
    }

}
#endif

/*
 * Handler functions for Message Passing
 */

static void handle_setup(uintptr_t* msg)
{
    uintptr_t core = get_client_id(msg);
    for (int i = 0; i < replica.num_clients; i++) {
        if (replica.clients[i] == core) {
            msg[4] = i;
        }
    }

    mp_send7(core,
             msg[0], msg[1], msg[2], msg[3], msg[4],
             msg[5], msg[6]);
}

static void handle_request(uintptr_t* msg) 
{
#ifdef MEASURE_TP
    if (!timer_started) {
        total_start = rdtsc();
        timer_started = true;	
    }	
    num_reqs++;
#endif
    if (replica.id == 0) {
        // TODO SEND BROADCAST
        set_tag(msg, BROAD_COMMIT);

#ifdef LIBSYNC
        mp_send_ab7(msg[0], msg[1], msg[2], msg[3], msg[4], msg[5],
                    msg[6]);
#else
        for (int i = 1; i < replica.num_replicas; i++) {
            mp_send7(replica.replicas[i], msg[0], msg[1], msg[2], msg[3],
                     msg[4], msg[5], msg[6]);
        }
#endif
        // send reply that broadcast is finished

        if (replica.level == NODE_LEVEL) {

            // execute request
            if (replica.alg_below != ALG_NONE) {
                com_layer_core_send_request(msg);
            }
   
            update_value(&msg[4]);
            mp_send7(replica.clients[get_client_id(msg)], msg[0], msg[1], 
                    msg[2], msg[3], msg[4], msg[5], msg[6]);
        } else {
            update_value(&msg[4]);
            mp_send7(replica.started_from, msg[0], msg[1], 
                    msg[2], msg[3], msg[4], msg[5], msg[6]);
            // TODO SEND TO OTHER REPLICA
        }
    } else {
        printf("Only leader should receive requests \n");
    }
}

static void handle_commit(uintptr_t* msg) 
{
    if (replica.id != 0) {
        // TODO forward request if we use a tree

        // execute request
        if (replica.alg_below != ALG_NONE) {
            com_layer_core_send_request(msg);
        }
        update_value(&msg[4]);
    } else {
        printf("Replica %d: leader should not receive commit\n", replica.id);
        return;
    }

}

void set_execution_fn_broadcast(void (*exec_fn)(void *))
{
    replica.exec_fn = exec_fn;
}

static void default_exec_fn(void* addr);
static void default_exec_fn(void* addr)
{
    return;
}

void init_replica_broadcast(uint8_t id,
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

    // connect to replicas
    if (id == 0) {
#ifndef LIBSYNC
        for (int i = 1; i < replica.num_replicas; i++) {
            mp_connect(current_core, replicas[i]);
        }
#endif
        if (replica.level == CORE_LEVEL) {
            mp_connect(current_core, replica.started_from);
        }
#ifdef MEASURE_TP
        pthread_t tid;
        pthread_create(&tid, NULL, results, &replica);
#endif
    } 


}

static void update_value(void* cmd)
{
    replica.exec_fn(cmd);
    return;
}

