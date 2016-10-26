/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012, 2013 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include "model_defs.h"
#include "quorum.h"
#include "barrelfish_compat.h"

#include <assert.h>
#include <stdio.h>
#include <stdbool.h>

/*
 * \brief A set of helper functions to parse the model.
 *
 * The model is given in model.h and model_defs.h.
 *
 */

extern int model[MODEL_NUM_CORES][MODEL_NUM_CORES];
extern int next_hop[MODEL_NUM_CORES][MODEL_NUM_CORES];
extern coreid_t num_cores;
extern coreid_t my_core_id;
extern __thread coreid_t this_core;

/*
 * \brief Check if there is a channel in the tree for the pair of nodes given
 *
 * Does NOT return true for connection LAST_NODE <-> SEQUENTIALIZER
 */
int model_is_edge(coreid_t src, coreid_t dest) 
{
    assert(src<num_cores);
    assert(dest<num_cores);

    // For MP: Node s -> r implies node r -> s
    /* QDBG("model_is_edge: src=%d dest=%d cost[src]=%d cost[dest]=%d\n", */
    /*      src, dest, model[src][dest], model[dest][src]); */

    assert(model[src][dest] == 0 || model[src][dest]>=SHM_SLAVE_START || model[dest][src]>0);
    assert(model[dest][src] == 0 || model[dest][src]>=SHM_SLAVE_START || model[src][dest]>0); // < fails

    return (model[src][dest]>0 && model[src][dest]<SHM_SLAVE_START) || 
        (model[src][dest]==99);
}

/*
 * \brief Checks wether given core c should participate in multicast
 */
bool model_is_in_group(coreid_t c)
{
    for (int i=0; i<MODEL_NUM_CORES; i++) {

        if (model[c][i]!=0) {

            return true;
        }
    }
    return false;
}

int model_get_num_cores_in_group(void)
{
    int n = 0;

    for (int i=0; i<MODEL_NUM_CORES; i++) {
        if(model_is_in_group(i)) {
            n++;
        }
    }

    return n;
}

/*!
 * \brief Return if \a parent is parent of \a child
 *
 * \param parent Parent core
 * \param core Child core
 */
inline int model_is_parent(coreid_t parent, coreid_t core)
{
    return model_is_edge(core, parent) && 
        model[core][parent] == 99;
}

int model_num_children(coreid_t c)
{
    uint32_t num = 0;
    
    for (int i=0; i<MODEL_NUM_CORES; i++) {

        if (model_is_parent(c, i)) 
            num++;
    }

    return num;
}

int model_num_neighbors(coreid_t c)
{
    uint32_t num = 0;
    
    for (int i=0; i<MODEL_NUM_CORES; i++) {

        if (model_is_parent(c, i) || model_is_parent(i, c))
            num++;
    }

    return num;
}



bool model_is_leaf(coreid_t core)
{
    bool leaf = true;

    for (int i=0; i<MODEL_NUM_CORES; i++) {
        
        leaf &= !model_is_parent(core, i);
    }

    return leaf;
}


int model_get_mp_order(coreid_t core, coreid_t child)
{
    assert (core<MODEL_NUM_CORES);
    assert (child<MODEL_NUM_CORES);

    if (!model_is_edge(core, child)) {

        return -1;
    }

    return model[core][child];
}

bool model_does_mp_send(coreid_t core)
{
    for (int i=0; i<MODEL_NUM_CORES; i++) {
        
        if (model_is_edge(core, i) && model_is_parent(core, i)) {

            return true;
        }
    }

    return false;
}

bool model_does_mp_receive(coreid_t core)
{
    for (int i=0; i<MODEL_NUM_CORES; i++) {
        
        if (model_is_edge(core, i) && !model_is_parent(core, i)) {

            return true;
        }
    }

    return false;
}

bool model_does_shm_send(coreid_t core)
{
    for (int i=0; i<MODEL_NUM_CORES; i++) {
        
        if (model[core][i]>=SHM_MASTER_START && model[core][i]<SHM_MASTER_MAX) {

            return true;
        }
    }

    return false;
}

bool model_does_shm_receive(coreid_t core)
{
    for (int i=0; i<MODEL_NUM_CORES; i++) {
        
        if (model[core][i]>=SHM_SLAVE_START && model[core][i]<SHM_SLAVE_MAX) {

            return true;
        }
    }

    return false;
}

void model_debug(coreid_t c1, coreid_t c2)
{
    printf("Parent: parent: %d edge: %d %d weight %d %d\n", 
           model_is_parent(c1,c2),
           model_is_edge(c1,c2),
           model_is_edge(c2,c1),
           model_get_mp_order(c1,c2),
           model_get_mp_order(c2,c1));
}

/*
 * \brief Check if there is an edge in the model to the given core
 *
 */
int model_has_edge(coreid_t dest) 
{
    return model_is_edge(my_core_id, dest);
}

int model_next_hop(coreid_t to)
{
    return next_hop[this_core][to];
}
