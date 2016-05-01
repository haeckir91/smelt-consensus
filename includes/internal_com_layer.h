/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
 * Attn: Systems Group.
 */
#ifndef _com_layer_h
#define _com_layer_h 1

#include <stdio.h>
#include <stdint.h>

/**
 * \brief initializing the layer between node level and core level algorithms
 * 
 * \param algorithm	which algorithm should be started on a subset of cores
 * \param replica_id	the replica id from which init was called
 * \param current_core	the current core we are running on
 * \param cores		array of core numbers on which the algorithm should 
 *			be started
 * \param num_cores	length of the array cores
 * \param cmd_size	mostly for shared memory part where the slots are fixed size
 * \param exec_func function that is executed after agreement on a value
 */

struct smlt_msg;
void com_layer_core_init(uint8_t algorithm, 
        uint8_t replica_id,
        uint8_t current_core,
        uint8_t* cores,
        uint8_t num_cores,
        uint16_t cmd_size, 
        void (*exec_func)(void*));

void com_layer_core_send_request(struct smlt_msg* msg);


#endif // _com_layer_h
