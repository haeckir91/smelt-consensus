/**
 * \file
 * \brief Interface for Two Phase Commit replica
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

#ifndef _replica_tpc_h
#define _replica_tpc_h 1

#include <stdint.h>
#include <stdbool.h>

void init_replica_tpc(uint8_t id, 
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
                      void (*exec_fn)(void*));

void message_handler_loop_tpc(void);
void set_execution_fn_tpc(void (*exec_fn)(void *));


#endif //_replica_tpc_h
