/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
 * Attn: Systems Group.
 */

#ifndef _consensus_client_h
#define _consensus_client_h 1

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define SETUP_TAG 0
#define REQ_TAG 1
#define RESP_TAG 2

/*
 * Initializes a client that can be used to send requets
 * to the consensus service 
 */
int init_consensus_client(void);
int consensus_send_request(uintptr_t* req);

uint16_t get_tag(uintptr_t* msg);
void set_tag(uintptr_t* msg, uint16_t tag);

uint16_t get_client_id(uintptr_t* msg);
void set_client_id(uintptr_t* msg, uint16_t cid);

uint32_t get_request_id(uintptr_t* msg);
void set_request_id(uintptr_t* msg, uint32_t client_id);


typedef struct benchmark_client_args_t{
    uint8_t core;
    uint8_t num_cores;
    uint8_t num_replicas;
    uint8_t num_clients;
    bool dummy;
    uint32_t sleep_time;
    uint8_t protocol;
    uint8_t protocol_below;
    uint8_t topo;
    uint8_t leader;
    uint8_t recv_from;
} benchmark_client_args_t;

void* init_benchmark_client(void* args);

int init_consensus_client_bench(int current_core,
                                int algo,
                                int algo_below,
                                int num_replicas,
                                int num_clients,
                                int topo,
                                int last_replica,
                                int leader);
#endif // _client_h
