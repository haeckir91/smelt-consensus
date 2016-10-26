/**
 * \file
 * \brief Implementation of shared memory qeueue writer
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
#include <stdbool.h>
#include <assert.h>

#include "shm.h"


//#define DEBUG_SHM
/*
 * The memory must already be shared between the
 * cluster cores
 *
 * \param shm Pointer to memory that should be used for the
 * queue. Presumably, this has to be SHMQ_SIZE*CACHELINE bytes.
 *
 * \param num_readers The number of readers using this queue. Each
 * reader has a read pointer, which has to be allocated.
 *
 * \param id An ID representing this reader, has to unique within a
 * cluster, starting at 0, i.e. for three readers, the id's would be
 * 0, 1 and 2.
 */
struct shm_queue* shm_init_context(void* shm,
                                   uint8_t num_readers,
                                   uint8_t id)
{

    struct shm_queue* queue = (struct shm_queue*) calloc(1, sizeof(struct shm_queue));
    assert (queue!=NULL);

    queue->shm = (uint8_t*) shm;
    queue->num_readers = num_readers;
    queue->id = id;

    queue->num_slots = SHMQ_SIZE;
#ifdef DEBUG_SHM
    queue->num_slots = 10;
#endif
    queue->data = (uint8_t*) shm;
    queue->l_pos = 0; 
  
    queue->r_mask = 0x1;
    queue->r_mask = queue->r_mask << id;
    
    queue->w_mask = 0x1;
    for (int i = 0; i < num_readers-1; i++) {
        queue->w_mask = queue->w_mask | (queue->w_mask << 1);
    }
    return queue;
}

void shm_send_raw(struct shm_queue* context,
                  uintptr_t p1,
                  uintptr_t p2,
                  uintptr_t p3,
                  uintptr_t p4,
                  uintptr_t p5,
                  uintptr_t p6,
                  uintptr_t p7)
{

    uintptr_t offset =  (context->l_pos*
                         (CACHELINE_SIZE/sizeof(uintptr_t)));
    assert (offset<SHMQ_SIZE*CACHELINE_SIZE);

    uintptr_t* slot_start = (uintptr_t*) context->data + offset;

    // if slot is not free ...
    while(slot_start[0] != 0) {};

    slot_start[1] = p1; 
    slot_start[2] = p2; 
    slot_start[3] = p3;
    slot_start[4] = p4; 
    slot_start[5] = p5;
    slot_start[6] = p6; 
    slot_start[7] = p7; 

#ifdef DEBUG_SHM
        printf("Shm writer %d: write pos %d val %ld \n", 
                sched_getcpu(), context->l_pos, slot_start[0]);
#endif
    // increse write pointer..
    uint64_t pos = context->l_pos+1;
    if (pos == context->num_slots) {
        context->l_pos = 0;
    } else {
        context->l_pos = pos;
    }
    
    slot_start[0] = context->w_mask;

}

// for now only for 64 readers
bool bit_is_set(uint8_t id, uint64_t start)
{
    uint64_t mask = 0x1;
    
    mask = mask << id;

    if ((start & mask) == 0) {
       return false;
    } else {
       return true;
    }
}


// returns NULL if reader reached writers.p
bool shm_receive_non_blocking(struct shm_queue* context,
              uintptr_t *p1,
              uintptr_t *p2,
              uintptr_t *p3,
              uintptr_t *p4,
              uintptr_t *p5,
              uintptr_t *p6,
              uintptr_t *p7)
{

    uintptr_t* start;
    start = (uintptr_t*) context->data + ((context->l_pos)*
                 CACHELINE_SIZE/(sizeof(uintptr_t)));

    if (bit_is_set(context->id, start[0])) {
        *p1 = start[1];
        *p2 = start[2];
        *p3 = start[3]; 
        *p4 = start[4]; 
        *p5 = start[5]; 
        *p6 = start[6]; 
        *p7 = start[7]; 
    } else {
        return false;
    }

    //TODO compare exchange header 
    uint64_t old = 0;
    uint64_t new_v = 0;

    while(true) {
        old = start[0];
        new_v = start[0] ^ context->r_mask;
        if (cas(start, old, new_v)) {
            break;
        } else {
            //printf("%016X %016X %016X\n", (unsigned int)start[0], old, new_v);
        }
    }       
#ifdef DEBUG_SHM
    printf("Shm reader %d: reader pos %d val %lu \n", 
                sched_getcpu(), context->l_pos, 
                start[1]);
#endif
    context->l_pos = context->l_pos + 1;

    if (context->l_pos == context->num_slots) {
        context->l_pos = 0;
    }
    return true;
}


// blocks
// TODO make this smarter?
void shm_receive_raw(struct shm_queue* context,
                     uintptr_t *p1,
                     uintptr_t *p2,
                     uintptr_t *p3,
                     uintptr_t *p4,
                     uintptr_t *p5,
                     uintptr_t *p6,
                     uintptr_t *p7)
{
    while(!shm_receive_non_blocking(context, p1, p2, p3,
                                 p4, p5, p6, p7)){};

}

