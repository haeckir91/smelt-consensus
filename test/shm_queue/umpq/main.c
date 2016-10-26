/**
 * \file
 * \brief Quorum implementation
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012, 2013 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <string.h>

#include "measurement_framework.h"
#include "quorum.h"
#include "model_defs.h"

/* #pragma GCC diagnostic ignored "-Wunused-variable" */
/* #pragma GCC diagnostic ignored "-Wunused-function" */
 
// --------------------------------------------------
// VARIABLES

coreid_t num_cores;
coreid_t my_core_id;
int my_node_id;
char app_name[1000];
char my_name[100];

struct sk_measurement m;

extern int model[MODEL_NUM_CORES][MODEL_NUM_CORES];

// ==================================================

/*
 * \brief Wait for clients to reach a certain round
 *
 * XXX This causes lot of interconnect traffic and might hence disturb
 * the measurement.
 *
 */
void qrm_exp_round(void)
{
    static int round = 0;
    assert(round<QRM_ROUND_MAX);

    // XXX Implement posix barrier here ..
}

static void init(void)
{
    // Set my core id
    my_core_id = disp_get_core_id();
    my_node_id = my_core_id+1;

    // Setup name to be used in quorum protocol
    sprintf(my_name, "quorum%d", my_core_id);
    QDBG("my name is %s\n", my_name);

    num_cores = get_num_cores();
    assert(num_cores <= Q_MAX_CORES);
}

int main_argc;
char **main_argv;

/*
 * \brief Benchmark program for atomic broadcast implementations
 *
 * \param argv First argument is the core from which to send messages
 */
int main(int argc, char *argv[])
{
    main_argc = argc;
    main_argv = argv;

    printf("Quorum mgmt: on core %d\n", disp_get_core_id());

    strcpy(app_name, argv[0]);
    init();

    mgmt_loop(NULL);
    printf("Quorum mgmt: everything done, exiting\n");

    return 0;
}
