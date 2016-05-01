/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
 * Attn: Systems Group.
 */
#ifndef _broadcast_h
#define _broadcast_h 1

#include <stdint.h>
#include <stdbool.h>
void init_replica_broadcast(uint8_t id,
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
                            void (*exec_fn)(void *));
void set_execution_fn_broadcast(void (*exec_fn)(void *));
void message_handler_loop_broadcast(void);

#endif //_broadcast_h
