/**
 * \file
 * \brief Implementation Two Phase Commit replica
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
#include <smlt.h>
#include <smlt_topology.h>
#include <smlt_node.h>
#include <smlt_broadcast.h>
#include <smlt_reduction.h>

#include "incremental_stats.h"
#include "crc.h"
#include "consensus.h"
#include "internal_com_layer.h"
#include "tpc_replica.h"
#include "client.h"


#define TPC_PREP 3
#define TPC_RDY 4
#define TPC_COM 5
#define TPC_VERIFY 15

typedef struct tpc_replica_t{	
    uint8_t id;
    uint8_t num_clients;
    uint8_t num_replicas;
    uint64_t num_requests;	
    void (*exec_fn) (void *);

    // next request in order
    // only for leader
    uint64_t index;

    // connections to replicas/clients
    uint8_t* clients;
    uint8_t* replicas;

    // for each client there can be only
    // one request around
    uint8_t *ready_counter;
    uint8_t *ack_counter;

    // composition
    uint8_t level;
    uint8_t alg_below;
    uint8_t node_size;
    uint8_t started_from_id;
    uint8_t *cores;
    uint8_t current_core;

    // smlt related
    struct smlt_context* ctx;
} tpc_replica_t;


__thread tpc_replica_t tpc_replica;

#ifdef VERIFY
__thread uint64_t* rid_history;
__thread uint16_t* cid_history;
__thread uint32_t* crcs;
__thread uint32_t crc_count;
#endif

static void handle_request(uintptr_t* msg);
static void handle_prepare(uintptr_t* msg);
static void handle_ready(uintptr_t* msg);
static void handle_commit(uintptr_t* msg);
static void handle_setup(uintptr_t* msg);
#ifdef VERIFY
static void handle_verify(uintptr_t* msg);
static crc_t verify(void);
#endif

static void update_value(uint64_t* cmd);

// Throughput mesaurement
#ifdef MEASURE_TP
static bool timer_started = false;
static uint64_t total_start;
static uint64_t total_end;

static uint64_t num_reqs = 0;
static uint8_t runs = 0;
static double run_res[7];
static incr_stats avg;

static void print_results_tpc(tpc_replica_t* rep) {

    init_stats(&avg);
    char* f_name = (char*) malloc(sizeof(char)*100);
#ifdef SMELT
    sprintf(f_name, "results/tp_%s_tpc_num_%d_numc_%d", topo_get_name(), 
            rep->num_replicas, rep->num_clients);
#else
    sprintf(f_name, "results/tp_tpc_below_%d_num_%d_numc_%d", 
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
    printf("To file \n");
#ifndef BARRELFISH
    fflush(f);
    fclose(f);
#endif
}

static void* results_tpc(void* arg)
{
    tpc_replica_t* rep = (tpc_replica_t*) arg;
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
            print_results_tpc(rep);
            break;
        }
        sleep(20);
    }   
    return 0;
}
#endif
/*
 * message handlers
 */
static void message_handler_tpc(uintptr_t *msg) 
{
    switch (get_tag(msg)) {
        case SETUP_TAG:
            handle_setup(msg);
            break; 

        case REQ_TAG:
            handle_request(msg);
            break; 

        case TPC_PREP:
            handle_prepare(msg);
            break; 

        case TPC_RDY:
            handle_ready(msg);
            break; 

        case TPC_COM:
            handle_commit(msg);
            break; 
        default:
            printf("unknown type in message_handler %lu \n", msg[0]);
    }
}

#ifdef SMLT

bool is_client(int n) 
{
    for (int i = 0; i < tpc_replica.num_clients; i++) {
        if (tpc_replica.clients[i] == n) {
            return true;
        }
    }
    return false;
}

void message_handler_loop_tpc(void)
{
    struct smlt_node node;
    errval_t err;

    struct smlt_msg* message = smlt_message_alloc(56);
    if (tpc_replica.id == 0) {
        int j = 0;
        int child_count[tpc_replica.num_clients];
        
        int num_c;
        int* nidx;
        mp_get_children(tpc_replica.replicas[0],
                        &num_c, &nidx);
        
        int* cores = (int*) malloc(sizeof(int)*tpc_replica.num_clients+num_c);
        for (int i = 0; i < tpc_replica.num_clients;i++) {
            cores[i] = tpc_replica.clients[i];
            child_count[i] = 0;
        }

        for (int i = 0; i < num_c; i++) {
            cores[i+tpc_replica.num_clients] = nidx[i];
        }


        while (true) {
            // TODO implement can recv
            if (true) {
                node = smlt_node_get_by_id(cores[j])
                err = smlt_node_recv(node, message);      
                mp_receive7(cores[j], message);
                if (get_tag(&message->data[0]) == SETUP_TAG) {
                    message_handler_tpc(message);
                } else {
                    if (!is_client(cores[j])) {
                       child_count[get_client_id(&message->data[0])]++;                    
                       if (child_count[get_client_id(&message->data[0])] == (num_c)) {
                            child_count[get_client_id(&message->data[0])] = 0;
                            set_tag(&message->data[0], TPC_RDY);
                            message_handler_tpc(message);
                       } 
                    } else {
                        message_handler_tpc(message);
                    }
               }

            }

            j++;

            j = j % (tpc_replica.num_clients+num_c);
        }
    
    } else {
        while (true) {
            // TODO context
            smlt_broadcast(ctx, message);
            if (get_tag(message->data[0]) == TPC_PREP) {
                set_tag(message->data[0], TPC_RDY);
                smlt_reduction(ctx, message);
            } else {
                message_handler_tpc(message);
            }
        }
    }
}

#else
void message_handler_loop_tpc(void)
{

    uintptr_t* message = (uintptr_t*) malloc(sizeof(uintptr_t)*8);
    if (tpc_replica.id == 0) {
        int j = 0;
        uint8_t* all_cores = (uint8_t*) malloc(sizeof(uint8_t)* (tpc_replica.num_replicas +
                                tpc_replica.num_clients));
        for (int i = 0; i < tpc_replica.num_replicas; i++) {
            all_cores[i] = tpc_replica.replicas[i];
        }

        for (int i = tpc_replica.num_replicas; i < (tpc_replica.num_replicas+ 
            tpc_replica.num_clients); i++) {
            all_cores[i] = tpc_replica.clients[(i-tpc_replica.num_replicas)];
        }
    
        while (true) {
            if (mp_can_receive(all_cores[j])) {
                mp_receive7(all_cores[j], message);
                message_handler_tpc(message);
            }    
            j++;

            j = j % (tpc_replica.num_replicas + tpc_replica.num_clients);
        }

    } else {
       
        while (true) {
            if (mp_can_receive(tpc_replica.replicas[0])) {
                mp_receive7(tpc_replica.replicas[0], message);
                message_handler_tpc(message);
            }    
        }
    }
}
#endif



/*
 * Message handlers
 */

#ifdef VERIFY
static void handle_verify(tpc_msg_t* msg) 
{
    crcs[msg->req.client_id] = msg->req.payload[0];
    crc_count++;

    if (crc_count == (tpc_replica.num_replicas-1)) {
        int num_wrong = 0;
        for (int i = 1; i < tpc_replica.num_replicas; i++) {
            printf("crcs[0] %d and crcs[%d] %d \n", crcs[0], i, crcs[i]);
            if (crcs[0] != crcs[i]) {
                num_wrong++;
            }	
        }

        if (num_wrong > 0) {
            printf("Implementation not correct \n");
        } else {
            printf("Implementation correct \n");
        }
    }
}
#endif

static void handle_setup(uintptr_t* msg)
{
    uintptr_t core = get_client_id(msg);
    for (int i = 0; i < tpc_replica.num_clients; i++) {
        if (tpc_replica.clients[i] == core) {
            msg[4] = i;
        }
    }

    mp_send7(core,
             msg[0], msg[1], msg[2], msg[3], msg[4],
             msg[5], msg[6]);
}


static void handle_request(uintptr_t* msg) 
{
#ifdef DEBUG_REPLICA
    printf("Replica %d: received request client %lu \n", replica.id, msg[1]);
#endif
#ifdef MEASURE_TP
    if (!timer_started) {
        total_start = rdtsc();
        timer_started = true;	
    }	
#endif
    if (tpc_replica.id == 0) {
        // reset counters for acks/ready messages
        tpc_replica.ready_counter[get_client_id(msg)] = 0;
        tpc_replica.ack_counter[get_client_id(msg)] = 0;

        // send to all replicas
        set_tag(msg, TPC_PREP);
#ifdef LIBSYNC
        mp_send_ab7(msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6]);
#else
        for (int i = 1; i < tpc_replica.num_replicas; i++) {
            mp_send7(tpc_replica.replicas[i], msg[0],
                     msg[1], msg[2], msg[3], msg[4] , msg[5],
                     msg[6]);
        }   
#endif
    } else {
        mp_send7(tpc_replica.replicas[0], msg[0], msg[1], msg[2], msg[3], msg[4],
                 msg[5], msg[6]);
    }
}

static void handle_prepare(uintptr_t* msg) 
{
#ifdef DEBUG_REPLICA
    printf("Replica %d: received prepare cid %lu, rid %lu\n", 
                                tpc_replica.id, msg[1], msg[2]);
#endif
    if (tpc_replica.id != 0) {
        set_tag(msg, TPC_RDY);
        mp_send7(tpc_replica.replicas[0], msg[0], msg[1], msg[2], msg[3],
                 msg[4], msg[5], msg[6]);
    } else {
        printf("Replica %d: leader shoult not receive prepare \n", 
                                                     tpc_replica.id);
        return;
    }
}

#ifdef LIBSYNC

static void handle_ready(uintptr_t* msg)
{
    if (tpc_replica.id == 0) {
        set_tag(msg, TPC_COM);
        msg[3] = tpc_replica.index;       
        tpc_replica.index++;
 
        mp_send_ab7(msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6]);    
  
        update_value(&msg[4]);

        set_tag(msg, RESP_TAG);
        mp_send7(tpc_replica.clients[get_client_id(msg)], msg[0], msg[1], 
                 msg[2], msg[3], msg[4], msg[5], msg[6]);

        // LEAF sends reply to client
#ifdef MEASURE_TP
        num_reqs++;
#endif
    } else {
        printf("Replica %d: Ready messages received \n", tpc_replica.id);
        return;
    }
}
#else
static void handle_ready(uintptr_t* msg)
{
#ifdef DEBUG_REPLICA
    printf("Replica %d: received ready cid %lu, rid %lu \n", 
                               tpc_replica.id, msg[1], msg[2]);
#endif
    if (tpc_replica.id == 0) {
        tpc_replica.ready_counter[get_client_id(msg)]++;   

        if (tpc_replica.ready_counter[get_client_id(msg)] >= 
             (tpc_replica.num_replicas-1)) {
            // TODO Broadcast COMMIT
            set_tag(msg, TPC_COM);
            tpc_replica.index++;
            msg[3] = tpc_replica.index;

            for (int i = 1; i < tpc_replica.num_replicas; i++) {
                mp_send7(tpc_replica.replicas[i], msg[0], msg[1], msg[2], 
                         msg[3], msg[4], msg[5], msg[6]);
            }
#ifdef VERIFY
            rid_history[replica.index] = msg[2];
            cid_history[replica.index] = msg[1];
#endif	
            update_value(&msg[4]);
            
            // send to CORE level            
            if ((tpc_replica.alg_below != ALG_NONE)) {
                com_layer_core_send_request(msg);
            }

            if (tpc_replica.level == NODE_LEVEL) {
                set_tag(msg, RESP_TAG);
                mp_send7(tpc_replica.clients[get_client_id(msg)], msg[0], msg[1], 
                         msg[2], msg[3], msg[4], msg[5], msg[6]);

            } else {
                set_tag(msg, RESP_TAG);
                mp_send7(tpc_replica.started_from_id, msg[0], msg[1], 
                         msg[2], msg[3], msg[4], msg[5], msg[6]);
            }
#ifdef MEASURE_TP
            num_reqs++;
#endif
            /*
#ifdef VERIFY
               if (replica.index == (replica.num_requests)) {		
                   crcs[0] = verify;            
                }
#endif
             */
            return;
        }
    } else {
            printf("Replica %d: Ready messages received \n", tpc_replica.id);
        return;
    }
}
#endif

static void handle_commit(uintptr_t* msg) 
{
    if (tpc_replica.id != 0) {
        // execute request
        update_value(&msg[4]);   
        if (tpc_replica.alg_below != ALG_NONE) {
            com_layer_core_send_request(msg);
        }
    } else {
        printf("Replica %d: leader shoult not receive commit \n",
               tpc_replica.id);
        return;
    }
}

/*
 * Verification and print results
 */
#ifdef VERIFY
crc_t verify(void) 
{
    crc_t crc;
    crc = crc_init();
    crc = crc_update(crc, (const unsigned char *) rid_history, sizeof(uint64_t)*replica.num_requests);
    crc = crc_update(crc, (const unsigned char *) cid_history, sizeof(uint16_t)*replica.num_requests);
    crc = crc_finalize(crc);
    return crc;
}
#endif
/*
 * Setting executing function
 */ 

void set_execution_fn_tpc(void (*exec_fn)(void *))
{
    tpc_replica.exec_fn = exec_fn;
}

static void default_exec_fn(void* addr);
static void default_exec_fn(void* addr)
{
    return;
}

/*
 * Replica init
 */ 
void init_replica_tpc(uint8_t id, 
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
                      void (*exec_fn)(void*))
{
    tpc_replica.id = id;
    tpc_replica.index = 0;
    tpc_replica.cores = cores;
    tpc_replica.num_clients = num_clients;
    tpc_replica.num_replicas = num_replicas;
    tpc_replica.level = level;
    tpc_replica.alg_below = alg_below;
    tpc_replica.node_size = node_size;
    tpc_replica.started_from_id = started_from;
    tpc_replica.current_core = current_core;
    tpc_replica.clients = clients;
    tpc_replica.replicas = replicas;

    if (exec_fn == NULL) {
        tpc_replica.exec_fn = &default_exec_fn;
    } else { 
        tpc_replica.exec_fn = exec_fn;
    }

    if (num_clients == 0) {
        tpc_replica.num_clients = 1;
    }

#ifdef VERIFY
    rid_history = malloc(sizeof(uint64_t)*(num_requests+1));
    cid_history = malloc(sizeof(uint16_t)*(num_requests+1));
    crcs = malloc(sizeof(uint32_t)*(num_replicas));
    crc_count = 0;
#endif

    if (id == 0) {
        tpc_replica.ready_counter = (uint8_t*) malloc(sizeof(uint8_t)*MAX_NUM_CLIENTS);
        tpc_replica.ack_counter = (uint8_t*) malloc(sizeof(uint8_t)*MAX_NUM_CLIENTS);	
    }

    // start algo below
    if (tpc_replica.alg_below != ALG_NONE) {
        com_layer_core_init(tpc_replica.alg_below, tpc_replica.id, 
                      tpc_replica.current_core, 
                      tpc_replica.cores, tpc_replica.node_size, 3*sizeof(uint64_t), 
                      tpc_replica.exec_fn);
    }

    if (id == 0) {
#ifndef LIBSYNC
        for (int i = 1; i < tpc_replica.num_replicas; i++) {
            mp_connect(current_core, replicas[i]);
        }
#endif
    }

#ifdef MEASURE_TP
    if ((id == 0) && (tpc_replica.level == NODE_LEVEL)) {
       pthread_t tid;
       pthread_create(&tid, NULL, results_tpc, &tpc_replica);
    }
#endif
}

static void update_value(uint64_t* cmd)
{
    tpc_replica.exec_fn((void *) cmd);
    return;
}

