/**
 * \brief Testing Shared memory queue
 */

/*
 * Copyright (c) 2015, ETH Zurich and Mircosoft Corporation.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>

#ifdef BARRELFISH
#include <barrelfish/barrelfish.h>
#include <vfs/vfs.h>
#include <numa.h>
#include <platforms/barrelfish.h>
#else
#include <platforms/linux.h>
#endif
#include <smlt.h>
#include <smlt_topology.h>
#include "internal_com_layer.h"
#include "consensus.h"

//#define DEBUG
static char default_path[] = "config.txt";

static void exec_fn(void* arg)
{
#ifdef DEBUG
    printf("Core %d \n", sched_getcpu());
#endif
    return;   
}

int main(int argc, char ** argv)
{
#ifdef BARRELFISH
    vfs_init();
    numa_available();
#endif

    int algo;
    int algo_below;
    char* config_path;
    int topo = 0;

    if (argc >= 3) {
       algo = atol(argv[1]);
       algo_below = atol(argv[2]);
       if (argc > 3) {
           config_path = argv[3];
       } else {
           config_path = default_path;
       }

       if (argc > 4) {
           topo = atol(argv[4]);
       }
    } else {
        algo = ALG_1PAXOS;
        algo_below = ALG_SHM;
        config_path = default_path;
        printf("Taking default Protocols \n");
    }

    FILE* f;
    f = fopen(config_path, "rw");
    
    if (f == NULL) {
        printf("No config.txt file found -> exit\n");
        exit(EXIT_FAILURE);
    } 

    // first line reads algorithm
    int num_replicas;
    int num_clients;
    int node_size;
    int num_cores;
    fscanf(f, "%d\n", &num_cores);
    fscanf(f, "%d\n", &num_replicas);
    fscanf(f, "%d\n", &node_size);
    fscanf(f, "%d\n", &num_clients);

    printf("############################################### \n");
    printf("Starting Benchmark \n");

    switch(algo) {
        case ALG_1PAXOS:
            printf("Protocol tier1 1Paxos \n");
            break;
        case ALG_TPC:
            printf("Protocol tier1 TPC \n");
            break;
        case ALG_BROAD:
            printf("Protocol tier1 BROAD \n");
            break;
        case ALG_CHAIN:
            printf("Protocol tier1 CHAIN \n");
            break;
        case ALG_RAFT:
            printf("Protocol tier1 RAFT \n");
            break;
        default:
            printf("Unkown Protocol tier1 \n");
            break;

    }   

    switch(algo_below) {
        case ALG_1PAXOS:
            printf("Protocol tier2 1Paxos \n");
            break;
        case ALG_TPC:
            printf("Protocol tier2 TPC \n");
            break;
        case ALG_BROAD:
            printf("Protocol tier2 BROAD \n");
            break;
        case ALG_SHM:
            printf("Protocol tier2 SHM \n");
            break;
        case ALG_NONE:
            printf("Protocol tier2 NONE \n");
            break;
        default:
            printf("Unkown Protocol tier2 \n");
            break;

    }   

    printf("%d top level replicas \n", num_replicas);
    printf("%d node size \n", node_size);
    printf("%d clients \n", num_clients);
    printf("############################################### \n");
    uint8_t cores[num_replicas];
    uint8_t cores2[num_replicas*node_size];
    memset(cores2, 0, sizeof(cores2));
    memset(cores, 0, sizeof(cores));

    int tmp;
    for (int i = 0 ; i < num_replicas; i++){
        for (int j = 0; j < node_size; j++) {
            if  (j == 0) {
                fscanf(f, "%d", &tmp);
                cores[i] = (uint8_t) tmp;
            } else if (j == node_size-1) {
                fscanf(f, "%d \n", &tmp);
                cores2[node_size*i+(j-1)] = (uint8_t) tmp;
            } else {
                fscanf(f, "%d ", &tmp);
                cores2[node_size*i+(j-1)] = (uint8_t) tmp;
            }
        }
    }


    printf("############################################### \n");
    printf("Tier1 Cores \n");
    for (int i = 0; i < num_replicas; i++)  {
        printf("%d ", cores[i]);
    }
    printf("\n");
    if (node_size > 1) {
        printf("Tier2 Cores \n");
        for (int i = 0; i < (num_replicas*node_size); i++)  {
            if (((i % node_size) == 0) && (i != 0)) {
                printf("\n");
            } else if ((i % node_size) == (node_size-1)) {
                continue;
            }
            printf("%d ", cores2[i]);
        }
        printf("\n");
    }
    printf("############################################### \n");

    printf("############################################### \n");
    
    uint8_t client_cores[num_clients];
    memset(client_cores, 0, sizeof(client_cores));
    for (int i = 0; i < num_clients; i++) {
        fscanf(f, "%d ", &tmp);
        client_cores[i] = (uint8_t) tmp;
    }   

    printf("Client on cores: \n");
    for (int i = 0; i < num_clients; i++) {
        printf("%d ", client_cores[i]);
    }
    printf("\n");
    printf("############################################### \n");
#ifdef LIBSYNC
    if (algo_below != ALG_NONE) {
        printf("Cant not start with LIBSYNC and an protocol on the lower layer \n");
        exit(0);
    }
#endif

    consensus_init(num_cores,
                   algo,
                   cores,
                   num_replicas,
                   num_clients,
                   algo_below,
                   node_size,
                   cores2,
                   client_cores,
                   exec_fn);

#ifdef LIBSYNC
    if (topo > 0) {
        switch_topo_to_idx(topo);  
    }
#endif

    sleep(5);
#ifdef DEBUG
    consensus_bench_clients_init(num_cores, client_cores, num_clients, 
                                 num_replicas, cores[num_replicas-1], 1, 
                                 algo, algo_below, topo, cores);
#else
    consensus_bench_clients_init(num_cores, client_cores, num_clients, 
                                 num_replicas, cores[num_replicas-1], 0, 
                                 algo, algo_below, topo, cores);
#endif

    // prevent from exit
    int runs = 0;
    while(true){
#ifdef BARRELFISH
        thread_yield();
#else
        pthread_yield();
#endif
        sleep(21);
        runs++;
        if (runs > 5){
            printf("Exit \n");
            break;
        }
       
    }

#ifdef BARRELFISH
        while(1)
            ;
#endif
    return 0;
}

