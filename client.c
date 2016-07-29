/**
 * \file
 * \brief Implementation of client side for the consensus service
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
#include <sched.h>
#include <smlt.h>
#include <smlt_message.h>
#include <smlt_debug.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "client.h"
#include "consensus.h"
#include "crc.h"
#include "incremental_stats.h"

typedef struct client_t{	
    int id;
    uint32_t request_count;
    uint8_t num_replicas;
    uint8_t num_clients;
    uint8_t algo;
    uint8_t algo_below;
    uint8_t current_core;
    uint8_t last_replica;
    uint8_t recv_from;
    bool replied;
    bool setup_done;

    uint32_t last_rid;
    uintptr_t* last_payload;
    struct smlt_msg* msg_buf;

    uint8_t current_leader;
    bool first;
    bool exit;
    uint8_t topo;

    incr_stats rt[6];
    incr_stats min_rt[6];
    incr_stats max_rt[6];

    uint8_t current_run;
} client_t;

static __thread client_t* client;

static void* measure_thread(void* args)
{
    client_t* c = (client_t*) args;
    int run = c->current_run;
    while(true) {
        if (!c->first) { 
            printf("Client %d: avg rt %10.7g, stdv %10.7g, 95 %% avg +- %10.7g, num_req %" PRIu32 " \n",
                    c->id, get_avg(&(c->rt[run-1])), get_std_dev(&(c->rt[run-1])), 
                    get_conf_interval(&(c->rt[run-1])), c->request_count);
            init_stats(&(c->rt[run]));
            if (c->id == 0) {
                printf("###############################################################"
                       "######################## \n");
            }
        }
        c->first = false;
        sleep(20);
        c->current_run++;
        run = c->current_run;

        if (run > 5) {
            c->exit = true;
            break;
        }
    }

    return 0;
}

int consensus_send_request(uintptr_t* payload)
{
    errval_t err;
    client->last_payload = payload;
    client->last_rid = client->request_count;
        
    set_tag(&client->msg_buf->data[0], REQ_TAG);
    set_client_id(&client->msg_buf->data[0], client->id);
    set_request_id(&client->msg_buf->data[0], client->request_count);
    client->msg_buf->data[4] = payload[0];
    client->msg_buf->data[5] = payload[1];
    client->msg_buf->data[6] = payload[2];
    client->msg_buf->data[3] = client->recv_from;

    err = smlt_send(client->current_leader, client->msg_buf);
    if (smlt_err_is_fail(err)) {
        // TODO
    }   

    err = smlt_recv(client->recv_from, client->msg_buf);
    if (smlt_err_is_fail(err)) {
        // TODO
    }
    client->request_count++;

    return 0;
}

int init_consensus_client(void)
{
    errval_t err;
    client->first = true;
    if (client->setup_done) {
        return 0;
    }

    client->exit = false;
    client->current_run = 0;
    client->id = -1;
    client->msg_buf = smlt_message_alloc(56);
    // set client information that is needed
    client->request_count = 0;
    client->id = -1;
    init_stats(&client->rt[0]);

    for (int i = 0; i < 6; i++) {
        init_stats(&(client->rt[i]));
    }

    set_tag(&client->msg_buf->data[0], SETUP_TAG);
    set_client_id(&client->msg_buf->data[0], client->current_core);

    err = smlt_send(client->current_leader, client->msg_buf);
    if (smlt_err_is_fail(err)) {
        // TODO
    }

    err = smlt_recv(client->current_leader, client->msg_buf);
    if (smlt_err_is_fail(err)) {
        // TODO
    }
    client->id = client->msg_buf->data[4];

    client->setup_done = true;

#ifdef BARRELFISH
    printf("Client %d: client initialized one core %d \n", client->id,
               disp_get_core_id());
#else
    printf("Client %d: client initialized one core %d \n", client->id,
           sched_getcpu());
#endif
    return client->id;
}


int init_consensus_client_bench(int current_core,
                                int algo,
                                int algo_below,
                                int num_replicas,
                                int num_clients,
                                int topo,
                                int last_replica,
                                int leader)
{
    client = (client_t* )calloc(1, sizeof(client_t));
    client->current_core = current_core;
    client->algo = algo;
    client->algo_below = algo_below;
    client->num_replicas = num_replicas;
    client->num_clients = num_clients;
    client->topo = topo;
    client->recv_from = last_replica;
    client->current_leader = leader;

    return init_consensus_client();
}

/*
 * Helper functions
 */

uint16_t get_tag(uintptr_t* msg)
{
    uint16_t* result = (uint16_t*) msg;
    return result[3];
}

void set_tag(uintptr_t* msg, uint16_t tag)
{
    uint16_t* result = (uint16_t*) msg;
    result[3] = tag;
}

uint16_t get_client_id(uintptr_t* msg)
{
    uint16_t* result = (uint16_t*) msg;
    return result[2];
}


void set_client_id(uintptr_t* msg, uint16_t cid)
{
    uint16_t* result = (uint16_t*) msg;
    result[2] = cid;
}

uint32_t get_request_id(uintptr_t* msg)
{
    uint32_t* result = (uint32_t*) msg;
    return result[0];
}

void set_request_id(uintptr_t* msg, uint32_t client_id)
{
    uint32_t* result = (uint32_t*) msg;
    result[0] = client_id;
}

#define F_NAME_LEN 100
static void print_results_file(void) {

    char* f_name = (char*) malloc(sizeof(char)*F_NAME_LEN);
    COND_PANIC(f_name!=NULL, "failed to allocate memory for filename");

    // Check if result directory exists
    struct stat st;
    snprintf(f_name, F_NAME_LEN, "results/rep_%d/", client->num_replicas);
    if (stat(f_name, &st) != 0) {

        printf("Making result directory\n");
        mkdir(f_name, 0777);
    }

#ifdef SMLT
    snprintf(f_name, F_NAME_LEN,
             "results/rep_%d/client_id_%d_algo_%d_below_%d_%s_num_%d",
            client->num_replicas, client->id, client->algo, client->algo_below,
            "adaptivetree", client->num_clients);
#else
    snprintf(f_name, F_NAME_LEN,
             "results/rep_%d/client_id_%d_algo_%d_below_%d_num_%d",
            client->num_replicas, client->id, client->algo, client->algo_below,
                    client->num_clients);
#endif
#ifndef BARRELFISH
    FILE* f = fopen(f_name, "a");
    COND_PANIC(f!=NULL, "could not open result file");
#endif
    RESULT_PRINTF(f, "Algo %d algo_below %d num_clients %d \n", 
            client->algo, client->algo_below,
            client->num_clients);
    incr_stats avg_avg, avg_stdv;
    init_stats(&avg_avg);
    init_stats(&avg_stdv);
    RESULT_PRINTF(f, "#####################################################");
    RESULT_PRINTF(f, "#####################\n");
    for (int i = 1; i < 6; i++) {
        RESULT_PRINTF(f, "avg rt %10.3f, stdv %10.3f, 95 %% avg +- %10.3f\n", 
                    get_avg(&(client->rt[i])), get_std_dev(&(client->rt[i])), 
                    get_conf_interval(&(client->rt[i])));
        add(&avg_avg, get_avg(&(client->rt[i])));
        add(&avg_stdv, get_std_dev(&(client->rt[i])));
    }

    RESULT_PRINTF(f, "\t avg \t avg_stdv \n"); 
    RESULT_PRINTF(f, "||\t%10.3f\t%10.3f\n", get_avg(&avg_avg), get_avg(&avg_stdv));
#ifndef BARRELFISH
    fflush(f);
    fclose(f);
#endif
}

/*
 * Start benchmark client
 */
static __thread uintptr_t payload[3];
void* init_benchmark_client(void* args) 
{
    benchmark_client_args_t* cl = (benchmark_client_args_t*) args;
    init_consensus_client_bench(cl->core,
                                cl->protocol,
                                cl->protocol_below,
                                cl->num_replicas,
                                cl->num_clients,
                                cl->topo,
                                cl->recv_from,
                                cl->leader);
#ifdef BARRELFISH
    printf("Client on core %d \n", disp_get_core_id());
#else
    printf("Client on core %d \n", sched_getcpu());
#endif

    pthread_t tid;
    pthread_create(&tid, NULL, measure_thread, client);

    uint64_t start;
    uint64_t end;

    while(!client->exit) {   
        if (cl->sleep_time > 0) {
           printf("Client %d: send request \n", client->id);
        }

        start = rdtsc();
        consensus_send_request(payload);
        end = rdtsc();
        // avoid scheduling measurements
        if ((end - start) < 500000) {
            add(&(client->rt[client->current_run]), (double) end - start);
        }

        sleep(cl->sleep_time);
    }

    print_results_file();
    printf("Client %d: exit \n", client->current_core);
    sleep(1);
    return 0;
}
