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
#include <numa.h>
#include <assert.h>

#include "consensus.h"
#include "kvs.h"

__thread int id;
__thread int kvs_size;
__thread int max_key;
void* kvs_memory[MAX_REPLICAS];

static void exec_fn(void* arg)
{
    uintptr_t* payload = (uintptr_t*) arg;    
    uintptr_t* kvs = (uintptr_t*) kvs_memory[id];

    if (payload[0] > (uintptr_t) max_key) {
        printf("Replica %d: Key too large %ld \n", id, payload[0]);
        return;
    }

//    printf("Writing to memory %d key %ld addr %p \n", id, payload[0], &kvs[2*payload[0]]);

    kvs[2*payload[0]] = payload[1];
    kvs[(2*payload[0])+1] = payload[2];
    return;   
}

void* init_kvs_replica(void* arg)
{

    struct cons_args_t* rep = (struct cons_args_t*) arg;
    id = rep->id;
    kvs_size = KVS_MEM_SIZE;
    max_key = kvs_size/(sizeof(uintptr_t)*2);
    kvs_memory[id] = numa_alloc_local(kvs_size);
    assert(kvs_memory[id] != NULL);
    rep->exec_func = exec_fn; 
  
    init_replica(arg);   
    return NULL;   
}

