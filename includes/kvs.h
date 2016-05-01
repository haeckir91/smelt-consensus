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
#ifndef _kvs_h
#define _kvs_h 1

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define MAX_REPLICAS 64
#define KVS_MEM_SIZE 16384
// shared memory for different replicas
extern void* kvs_memory[MAX_REPLICAS];

void* init_kvs_replica(void* arg);

struct kvs_value {
    uintptr_t v1;
    uintptr_t v2;
};

/*
 * kvs_get and kvs_set return the number of cylce it took to
 * get/set a value of the key value store
 */
uint64_t kvs_get(uintptr_t key, struct kvs_value* val);
uint64_t kvs_set(uintptr_t key, struct kvs_value* val);

void* init_benchmark_kvs_client(void* args);

#endif // _kvs_h

