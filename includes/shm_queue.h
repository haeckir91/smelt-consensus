/**
 * \file
 * \brief Interface for a shared memory queue
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

#ifndef _shm_queue_h
#define _shm_queue_h 1

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

//#define DEBUG_SHM

/**
 * \brief initializing a shared memory queue writer
 *
 * \param replica_id	for multiple instances of this SHM queue, the replica_id prevents
 * 			these isntances to communicate with each other. In the consensus implementatio
 * 			used for differentiating between nodes
 * \param current_core	the core on which the writer should run
 * \param num_replicas	number of readers for setting up the memory
 * \param num_clients	number of clients that connect to the leader
 * \param slot_size	Size of a slot of the queue
 * \param shared_mem   the shared memory used for the queue
 * \param node_level	Is this writer running on the node level?
 * \param exec_fn	execution function
 */
void init_shm_writer(uint8_t replica_id, 
          uint8_t current_core,
          uint8_t num_clients,
	      uint8_t num_replicas,
 	      uint64_t slot_size,
	      bool node_level,	    
          void* shared_mem,
	      void (*exec_fn)(void *));
/**
 * \brief initializing a shared memory queue reader
 *
 * \param id	       the readers id
 * \param current_core the core on which the writer should be started
 * \param num_replicas the number of replicas (require for setting up the shared
 *                      memory)
 * \param slot_size    the size of the slots of the queue
 * \param node_level   Is this reader running on the node level?
 * \param started_from the replica id which started the readers
 * \param shared_mem   the shared memory used for the queue
 * \param exec_fn	   execution function
 */
void init_shm_reader(uint8_t id, 
                     uint8_t current_core,
                     uint8_t num_replicas, 
                     uint64_t slot_size,
                     bool node_level,
                     uint8_t started_from,
                     void* shared_mem,
                     void (*exec_fn)(void* addr));
void shm_write(void* addr);
void* shm_read(void);


void set_execution_fn_shm(void (*execute)(void * addr));

void poll_and_execute(void);
#endif // _shm_queue_h
