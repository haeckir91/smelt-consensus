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
#include <numa.h>
#include <pthread.h>
#include <smlt.h>
#include <smlt_message.h>

#include "client.h"
#include "consensus.h"
#include "kvs.h"
#include "incremental_stats.h"


struct kvs_client {
    int id;
    int num_clients;
    uintptr_t* local_mem;
    int run;
    int first;
    bool exit;
    uint64_t num_reads;
    uint64_t num_writes;
    uint64_t num_large;
    incr_stats w_rt[7];
    incr_stats r_rt[7];
    incr_stats r_tp;
    incr_stats w_tp;
};

extern void* kvs_memory[MAX_REPLICAS];

static __thread struct kvs_client* client;

static void* measure_thread(void* args)
{
    struct kvs_client* c = (struct kvs_client*) args;
    while(true) {
        if (!c->first) {

            printf("Client %d: w_rt %10.3f, r_rt %10.3f, w_tp %10.f, r_tp %10.3f, stdv %10.3f large %10.3f\n",
                    c->id, get_avg(&(c->w_rt[c->run-1])), get_avg(&(c->r_rt[c->run-1])),
                    (double) c->num_writes/20, (double) c->num_reads/20,
                    get_std_dev(&(c->w_rt[c->run-1])), (double)c->num_large/20);

            if (c->id == 0) {
                printf("###############################################################");
                printf("######################## \n");
            }

            if (c->run > 1) {
                add(&(c->r_tp), (double) c->num_reads/20);
                add(&(c->w_tp), (double) c->num_writes/20);
            }
        }

        c->first = false;
        c->run++;
        c->num_reads = 0;
        c->num_writes = 0;
        c->num_large = 0;
        sleep(20);

        if (c->run > 5) {
            c->exit = true;
            break;
        }
    }

    return 0;
}


static void print_results_file(void) {

    char* f_name = (char*) malloc(sizeof(char)*100);
#ifdef SMLT
    sprintf(f_name, "results/client_kvs_%s_id_%d_num_%d",
                    topo_get_name(), client->id, client->num_clients);

#else
    sprintf(f_name, "results/client_kvs_id_%d_num_%d",
                    client->id, client->num_clients);
#endif
#ifndef BARRELFISH
    FILE* f = fopen(f_name, "w+");
#endif
    RESULT_PRINTF(f, "Client id %d num_clients %d \n",
            client->id, client->num_clients);
    incr_stats r_rt_avg, r_rt_stdv;
    init_stats(&r_rt_avg);
    init_stats(&r_rt_stdv);

    incr_stats w_rt_avg, w_rt_stdv;
    init_stats(&w_rt_avg);
    init_stats(&w_rt_stdv);


    RESULT_PRINTF(f, "#####################################################");
    RESULT_PRINTF(f, "#####################\n");
    for (int i = 2; i < 6; i++) {
        RESULT_PRINTF(f, "w_rt %10.3f stdv %10.3f, r_rt %10.3f stdv %10.3f \n",
                    get_avg(&(client->w_rt[i])), get_std_dev(&(client->w_rt[i])),
                    get_avg(&(client->r_rt[i])), get_std_dev(&(client->r_rt[i])));

        add(&w_rt_avg, get_avg(&(client->w_rt[i])));
        add(&w_rt_stdv, get_std_dev(&(client->w_rt[i])));

        add(&r_rt_avg, get_avg(&(client->r_rt[i])));
        add(&r_rt_stdv, get_std_dev(&(client->r_rt[i])));
    }

    RESULT_PRINTF(f, "\t w_rt \t stdv \t w_tp \t stdv \t r_rt \t stdv \t r_tp \t stdv\n");
    RESULT_PRINTF(f, "||\t%10.3f\t%10.3f\t%10.3f\t%10.3f\t%10.3f\t%10.3f\t%10.3f\t%10.3f\n",
                  get_avg(&w_rt_avg), get_avg(&w_rt_stdv),
                  get_avg(&(client->w_tp)), get_std_dev(&(client->w_tp)),
                  get_avg(&r_rt_avg), get_avg(&r_rt_stdv),
                  get_avg(&(client->r_tp)), get_std_dev(&(client->r_tp)));
#ifndef BARRELFISH
    fflush(f);
    fclose(f);
#endif
}

// TODO remove uint64_t return value
uint64_t kvs_get(uintptr_t key, struct kvs_value* val)
{
    val->v1 = client->local_mem[key*2];
    val->v2 = client->local_mem[(key*2)+1];
    return 0;
}

// TODO remove uint64_t return value
static __thread uintptr_t payload[3];
uint64_t kvs_set(uintptr_t key, struct kvs_value* val)
{
    payload[0] = key;
    payload[1] = val->v1;
    payload[2] = val->v2;
    consensus_send_request(payload);
    return 0;

}

int init_kvs_client(int current_core,
                    int algo,
                    int algo_below,
                    int num_replicas,
                    int num_clients,
                    int topo,
                    int last_replica,
                    int leader)
{

    client = (struct kvs_client*) malloc(sizeof(struct kvs_client));
    int numa_node = numa_node_of_cpu(current_core);
    client->local_mem = (uintptr_t*) kvs_memory[numa_node];
    assert(client->local_mem != NULL);
    client->first = true;
    client->exit = false;
    client->num_reads = 0;
    client->num_large = 0;
    client->num_writes = 0;
    client->num_clients = num_clients;
    client->id = init_consensus_client_bench(current_core,
                                algo,
                                algo_below,
                                num_replicas,
                                num_clients,
                                topo,
                                last_replica,
                                leader);
    return client->id;
}

/*
 * Start benchmark client
 */
void* init_benchmark_kvs_client(void* args)
{
    benchmark_client_args_t* cl = (benchmark_client_args_t*) args;
    init_kvs_client(cl->core,
                    cl->protocol,
                    cl->protocol_below,
                    cl->num_replicas,
                    cl->num_clients,
                    cl->topo,
                    cl->recv_from,
                    cl->leader);

#ifdef BARRELFISH
    printf("KVS client on core %d \n", disp_get_core_id());
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    cpu_set_t *cpuset = CPU_ALLOC(client->current_core+1);
    CPU_ZERO(cpuset);
    CPU_SET(client->current_core, cpuset);
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), cpuset);
#else
    printf("KVS client on core %d \n", sched_getcpu());

    pthread_t tid;
    pthread_create(&tid, NULL, measure_thread, client);
#endif

    for (int i = 0; i < 7; i++) {
        init_stats(&(client->r_rt[i]));
        init_stats(&(client->w_rt[i]));
    }
    init_stats(&(client->r_tp));
    init_stats(&(client->w_tp));

    struct kvs_value* val = (struct kvs_value*) malloc(sizeof(struct kvs_value));
    uint64_t start, end;
    int key;
    while(!client->exit) {
        key = (rand() % 50);
        val->v1 = key;
        val->v2 = 22;
        for (int i = 0; i < 2; i++) {
            start = rdtsc();
            kvs_set(key, val);
            end = rdtsc();
            client->num_writes++;
            if ((end-start) < 500000) {
                add(&(client->w_rt[client->run]), (double) end - start);
            } else {
                client->num_large++;
            }
        }

        for (int i = 0; i < 8; i++) {
            start = rdtsc();
            kvs_get(key, val);
            end = rdtsc();
            add(&(client->r_rt[client->run]), (double) end - start);
            client->num_reads++;
        }
    }

    printf("Client %d: exit \n", cl->core);
    print_results_file();
    sleep(1);
    return 0;
}
