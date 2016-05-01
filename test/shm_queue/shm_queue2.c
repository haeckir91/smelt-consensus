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

    queue->num_slots = (SHMQ_SIZE)-(num_readers+2);
#ifdef DEBUG_SHM
    queue->num_slots = 10;
#endif
    queue->write_pos = (union pos_pointer*) shm;
    queue->readers_pos = (union pos_pointer*) shm+1;
    queue->data = (uint8_t*) shm+((num_readers+2)*sizeof(union pos_pointer));

    
    return queue;
}

static bool can_write(struct shm_queue* context, int id) {
/*
    printf("%d \n", (((context->readers_pos[id].p[0] <= (context->write_pos[0].p[0])) &&
        (context->readers_pos[id].p[1] == context->write_pos[0].p[1]))));
    printf("%d \n", (((context->readers_pos[id].p[0] > context->write_pos[0].p[0]) &&
        (context->readers_pos[id].p[1] != context->write_pos[0].p[1]))));
*/
    return (((context->readers_pos[id].p[0] <= context->write_pos[0].p[0]) &&
        (context->readers_pos[id].p[1] == context->write_pos[0].p[1])) 
        || ((context->readers_pos[id].p[0] > context->write_pos[0].p[0]) &&
        (context->readers_pos[id].p[1] != context->write_pos[0].p[1]))); 
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
    for (int i = 0; i < context->num_readers; i++) {
        // check if reader is in the same
        while (!can_write(context, i)) {};
    }


    uintptr_t offset =  (context->write_pos[0].p[0]*
                         (CACHELINE_SIZE/sizeof(uintptr_t)));
    assert (offset<SHMQ_SIZE*CACHELINE_SIZE);

    uintptr_t* slot_start = (uintptr_t*) context->data + offset;

    slot_start[0] = p1;
    slot_start[1] = p2; 
    slot_start[2] = p3; 
    slot_start[3] = p4;
    slot_start[4] = p5; 
    slot_start[5] = p6;
    slot_start[6] = p7; 

#ifdef DEBUG_SHM
        printf("Shm writer %d: write.p %" PRIu64 " val %lu epoch %lu \n", 
                sched_getcpu(), context->write_pos[0].p[0], 
                slot_start[0], context->write_pos[0].p[1]);
#endif
    // increse write pointer..
    uint64_t pos = context->write_pos[0].p[0]+1;
    if (pos == context->num_slots) {
        if (context->write_pos[0].p[1] == 1) {
            context->write_pos[0].p[1] = 0;
        } else {
            context->write_pos[0].p[1] = 1;
        }
        context->write_pos[0].p[0] = 0;
    } else {
        context->write_pos[0].p[0] = pos;
    }
}

static bool can_read(struct shm_queue* context) {
    // true if write pointer is greater and we are in the same epoch
    // or if write pointer is smaller and we are not in the same epoch
    return (((context->write_pos[0].p[0] > context->readers_pos[context->id].p[0])
        && (context->write_pos[0].p[1] == context->readers_pos[context->id].p[1]))
        || ((context->write_pos[0].p[0] <= context->readers_pos[context->id].p[0])
        && (context->write_pos[0].p[1] != context->readers_pos[context->id].p[1])));
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
    if (can_read(context)) {
        uintptr_t* start;
        start = (uintptr_t*) context->data + ((context->readers_pos[context->id].p[0])*
                 CACHELINE_SIZE/(sizeof(uintptr_t)));
        *p1 = start[0];
        *p2 = start[1];
        *p3 = start[2]; 
        *p4 = start[3]; 
        *p5 = start[4]; 
        *p6 = start[5]; 
        *p7 = start[6]; 

#ifdef DEBUG_SHM
        printf("Shm reader %d: reader.p %" PRIu64 " val %lu \n", 
                sched_getcpu(), context->readers_pos[context->id].p[0], 
                start[0]);
#endif
        context->readers_pos[context->id].p[0] = 
                    context->readers_pos[context->id].p[0] + 1;

        if (context->readers_pos[context->id].p[0] == context->num_slots) {
            if (context->readers_pos[context->id].p[1] == 1) {
                context->readers_pos[context->id].p[1] = 0;
            } else {
                context->readers_pos[context->id].p[1] = 1;
            }
            context->readers_pos[context->id].p[0] = 0;
        }
        return true;
    } else {
        return false;
    }
    return false;
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

