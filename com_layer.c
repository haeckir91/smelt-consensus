/**
 * \file
 * \brief Communication Layer for Node to Core layer communication as 
 * 	  well as starting the whole consensus hierarchy
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
#include <pthread.h>
#include <numa.h>
#include <smlt.h>
#include <smlt_node.h>
#include <smlt_message.h>
#include <smlt_topology.h>
#include <smlt_generator.h>
#include <smlt_context.h>


#include "consensus.h"
#include "client.h"
#include "internal_com_layer.h"
#include "shm_queue.h"
#include "kvs.h"

typedef struct com_layer_t{	
    uint8_t algorithm;
    char* replica_string;
    uint8_t replica_id;
    uint8_t num_clients;
    uint8_t* cores;
    uint8_t core_to_send_to;
    uint8_t* client_cores;
    // one single array of cores for nodes 
    uint8_t* node_cores;
    uint8_t num_cores;
    uint16_t cmd_size;
    uint8_t alg_below;
    uint8_t node_size;
    struct waitset* ws;

    uint8_t current_core;
    bool init_done;

    uint64_t req_count;
    void (*exec_func) (void *);

    // shared memory for SHM queue
    void* shared_mem;
} com_layer_t;

static __thread com_layer_t com_core;
static __thread com_layer_t com_node;
static __thread cons_args_t thr_args[64];
static __thread cons_args_t thr_args2[64];
static void* (*replica_function) (void*);
static void* (*client_function) (void*);

extern struct smlt_context* ctx;
extern struct smlt_topology* topo;

/*
 * Startup protocol function
 */
static void init_protocol_core(uint8_t algorithm)
{
    errval_t err;
    struct smlt_node *node;

    com_core.num_cores--;

    for (int i = 1; i < com_core.num_cores; i++) {
        thr_args2[i].num_clients = 1;
        thr_args2[i].num_replicas = com_core.num_cores;
        thr_args2[i].algo = algorithm;
        thr_args2[i].level = CORE_LEVEL;
        thr_args2[i].alg_below = ALG_NONE;
        thr_args2[i].num_requests = 0;
        thr_args2[i].started_from = com_core.current_core;
        thr_args2[i].exec_func = com_core.exec_func;
        thr_args2[i].id = i;
        thr_args2[i].current_core = com_core.cores[i];
        thr_args2[i].shared_mem = com_core.shared_mem;
        thr_args2[i].replicas = com_core.cores;
        thr_args2[i].clients = &com_core.current_core;
        
        node = smlt_get_node_by_id(com_core.cores[i]);
        err = smlt_node_start(node, replica_function, (void*) &thr_args[i]);
        if (smlt_err_is_fail(err)) {
            printf("Staring node failed \n");
        }

    }

    // at the end start leader so he can directly connect to every replica
    thr_args2[0].num_clients = 1;
    thr_args2[0].num_replicas = com_core.num_cores;
    thr_args2[0].algo = algorithm;
    thr_args2[0].level = CORE_LEVEL;
    thr_args2[0].alg_below = ALG_NONE;
    thr_args2[0].shared_mem = com_core.shared_mem;
    thr_args2[0].num_requests = 0;
    thr_args2[0].started_from = com_core.current_core;
    thr_args2[0].current_core = com_core.cores[0];
    thr_args2[0].exec_func = com_core.exec_func;
    thr_args2[0].id = 0;
    thr_args2[0].replicas = com_core.cores;
    thr_args2[0].clients = &com_core.current_core;

    node = smlt_get_node_by_id(com_core.cores[0]);
    err = smlt_node_start(node, replica_function, (void*) &thr_args[0]);
    if (smlt_err_is_fail(err)) {
        printf("Staring node failed \n");
    }

    // init connection, only to leader since failure domain is a node
    com_core.init_done = true;
}

static void init_protocol_node(uint8_t algorithm)
{
    struct smlt_node* node;
    errval_t err;
    printf("################## Node 0 ##################\n");
    // at the end start leader so he can directly connect to every replicaa
    thr_args[0].num_clients = com_node.num_clients;
    thr_args[0].num_replicas = com_node.num_cores;
    thr_args[0].algo = algorithm;
    thr_args[0].level = NODE_LEVEL;
    thr_args[0].alg_below = com_node.alg_below;
    thr_args[0].num_requests = 0;
    thr_args[0].node_size = com_node.node_size;
    thr_args[0].started_from = 0;
    thr_args[0].cores = com_node.node_cores;
    thr_args[0].exec_func = com_node.exec_func;
    thr_args[0].id = 0;
    thr_args[0].current_core = com_node.cores[0];
    thr_args[0].replicas = com_node.cores;
    thr_args[0].clients = com_node.client_cores;

    node = smlt_get_node_by_id(com_core.cores[0]);
    err = smlt_node_start(node, replica_function, (void*) &thr_args[0]);
    if (smlt_err_is_fail(err)) {
        printf("Staring node failed \n");
    }

    sleep(2);
    uint8_t** core_tmp = (uint8_t**) malloc(sizeof(uint8_t*) * com_node.num_cores);
    for (int i = 1; i < com_node.num_cores; i++) {
        printf("################## Node %d ##################\n",i);
        //thr_args[i] = (cons_args_t*) malloc(sizeof(cons_args_t));
        core_tmp[i] = (uint8_t*) malloc(sizeof(uint8_t)* com_node.num_cores);

        thr_args[i].num_clients = com_node.num_clients;
        thr_args[i].num_replicas = com_node.num_cores;
        thr_args[i].algo = algorithm;
        thr_args[i].level = NODE_LEVEL;
        thr_args[i].alg_below = com_node.alg_below;
        thr_args[i].num_requests = 0;
        thr_args[i].node_size = com_node.node_size;
        thr_args[i].started_from = 0;
        thr_args[i].cores = com_node.node_cores;
        thr_args[i].exec_func = com_node.exec_func;
        thr_args[i].replicas = com_node.cores;

        for (int j = 0; j < com_node.node_size;j++) {
            core_tmp[i][j] = com_node.node_cores[i*com_node.node_size+j];
        } 

        thr_args[i].cores = core_tmp[i];
        thr_args[i].clients = com_node.client_cores;
        thr_args[i].current_core = com_node.cores[i];
        thr_args[i].id = i;


        node = smlt_get_node_by_id(com_core.cores[i]);
        err = smlt_node_start(node, replica_function, (void*) &thr_args[i]);
        if (smlt_err_is_fail(err)) {
            printf("Staring node failed \n");
        }

        if (com_node.node_size > 1) {
            sleep(2);
        }
    }

    // give it some time to start
    sleep(1);
    com_node.init_done = true;
}

/*
 * Interface functions
 */

void com_layer_core_init(uint8_t algorithm, 
        uint8_t replica_id,
        uint8_t current_core,
        uint8_t* cores,
        uint8_t num_cores,
        uint16_t cmd_size,
        void (*exec_fn)(void*))
{
    com_core.algorithm = algorithm;
    com_core.replica_id = replica_id;
    com_core.cores = cores;
    com_core.num_cores = num_cores;
    com_core.cmd_size = cmd_size;
    com_core.req_count = 0;
    com_core.exec_func = exec_fn;
    com_core.shared_mem = calloc(1, 4096);
    com_core.current_core = current_core;
    com_core.core_to_send_to = cores[0]; 
   

    if (algorithm == ALG_SHM) {
        init_shm_writer(replica_id, current_core, 1, num_cores-1, 
                        cmd_size, false, com_core.shared_mem, exec_fn);
        init_protocol_core(ALG_SHM);
        com_core.init_done = true;
    } else {
        if (algorithm < 7) {
            init_protocol_core(algorithm);
        } else {
            printf("Com Layer: Unknown algorithm \n");
            return;
        }
    }

    //mp_connect(current_core, cores[0]);
    com_core.init_done = true;
}

#ifdef BARRELFISH
static void domain_init_done(void *arg, errval_t err)
{
    debug_printf("SPANNED!\n");
}
#endif

void consensus_init(
        uint8_t total_cores,
        uint8_t algorithm, 
        uint8_t* cores,
        uint8_t num_cores,
        uint8_t num_clients,
        uint8_t alg_below,
        uint8_t node_size,
        uint8_t* node_cores,
        uint8_t* client_cores,
        void (*exec_fn)(void*))
{
    errval_t err;
    if ((algorithm == ALG_SHM) && (alg_below) != ALG_NONE) {
        printf("Can not start SHARED MEMORY on node level with \
                other algorithm on core level");
        return;
    }

#ifdef BARRELFISH
    for (uint8_t i = 0; i < num_cores; i++) {
        if (cores[i] == disp_get_core_id()) {
            continue;
        }
        printf("Spannign domain to core %d\n", cores[i]);
        errval_t err = domain_new_dispatcher(cores[i], domain_init_done, NULL);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "failed to span domain");
            printf("Failed to span domain to %d\n", cores[i]);
            assert(err_is_ok(err));
        }
    }

    for (uint8_t i = 0; i < num_clients; i++) {
        if (client_cores[i] == disp_get_core_id()) {
            continue;
        }
        printf("Spannign domain to core %d (client)\n", client_cores[i]);
        errval_t err = domain_new_dispatcher(client_cores[i], domain_init_done, NULL);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "failed to span domain");
            printf("Failed to span domain to %d\n", client_cores[i]);
            assert(err_is_ok(err));
        }
    }
#endif
    
#ifdef KVS
    replica_function = init_kvs_replica;
    client_function = init_benchmark_kvs_client;
#else
    replica_function = init_replica;
    client_function = init_benchmark_client;
#endif


    com_node.algorithm = algorithm;
    com_node.cores = cores;
    com_node.node_cores = node_cores;
    com_node.num_cores = num_cores;
    com_node.alg_below = alg_below;
    com_node.node_size = node_size;
    com_node.num_clients = num_clients;
    com_node.exec_func = exec_fn;
    com_node.client_cores = client_cores;

    err = smlt_init(total_cores, true);
    if (smlt_err_is_fail(err)) {
        printf("FAILED TO INITIALIZE !\n");
        return;
    }

    struct smlt_generated_model* model = NULL;
    uint32_t* cores_cpy = malloc(sizeof(uint32_t)*num_cores);
    
    for (int i = 0; i < num_cores; i++) {
        cores_cpy[i] = cores[i];
    }

    err = smlt_generate_model(cores_cpy, num_cores, "adaptivetree", &model);
    if (smlt_err_is_fail(err)) {
        printf("Failed to generated model, aborting\n");
        return;
    }

    struct smlt_topology *topo = NULL;
    smlt_topology_create(model, "adaptivetree", &topo);
    err = smlt_context_create(topo, &ctx);
    if (smlt_err_is_fail(err)) {
        printf("FAILED TO INITIALIZE CONTEXT !\n");
        return;
    }
    

    if (algorithm < 7) {
        init_protocol_node(algorithm);
    } else {
        printf("Com Layer: Unknown algorithm \n");
    }
}

// TODO init this buffer!
static __thread struct smlt_msg buf;
void com_layer_core_send_request(struct smlt_msg* msg)
{
    errval_t err;
    struct smlt_node* node;
    if (!com_core.init_done) {
        printf("Com Layer: Can not send request to core layer, not initialized yet \n");
        return;
    }
    
    if (com_core.algorithm== ALG_SHM) {
        shm_write(&msg->data[4]);
    } else {
        // save client id since we change it
        uint8_t cid = get_client_id(&msg->data[0]);

        // send message to lower layer
        set_tag(&msg->data[0], REQ_TAG);
        set_client_id(&msg->data[0], 0);
        set_request_id(&msg->data[0], com_core.req_count);
    
        node = smlt_get_node_by_id(com_core.cores[0]);
        err = smlt_node_send(node, msg);
        if (smlt_err_is_fail(err)) {
            // TODO;
        }       

        err = smlt_node_recv(node, &buf);
        if (smlt_err_is_fail(err)) {
            // TODO;
        }       
        set_client_id(&msg->data[0], cid);
    }
    com_core.req_count++;
}

/*
 * Benchmark client specific init
 */


static __thread benchmark_client_args_t args[64];
void consensus_bench_clients_init(uint8_t num_cores,
                                  uint8_t* cores,
                                  uint8_t num_clients,
                                  uint8_t num_replicas,
                                  uint8_t last_replica,
                                  uint64_t sleep_time,
                                  uint8_t protocol,
                                  uint8_t protocol_below,
                                  uint8_t topo,
                                  uint8_t* replica_cores)
{
    errval_t err;
    struct smlt_node* node;
    for (int i = 0; i < num_clients; i++) {
        args[i].core = cores[i];
        args[i].sleep_time = sleep_time;
        args[i].num_cores = num_cores;
        args[i].num_clients = num_clients;
        args[i].num_replicas = num_replicas;
        args[i].dummy = false;
        args[i].protocol = protocol;
        args[i].protocol_below = protocol_below;
        args[i].topo = topo;
        if (protocol != ALG_1PAXOS) {
            args[i].leader = replica_cores[0];
            if (protocol != ALG_CHAIN) {
                args[i].recv_from = replica_cores[0];
            } else {
                args[i].recv_from = replica_cores[num_replicas-1];
            }
        } else { 
            int numa = numa_node_of_cpu(cores[i]);
//#ifdef SMELT
            args[i].leader = replica_cores[1];
            args[i].recv_from = replica_cores[1];
/*#else
            args[i].leader = replica_cores[0];
            args[i].recv_from = replica_cores[0];
#endif
*/
            if ((numa == 1) || (numa == 0)) {
                numa = rand() % (num_replicas-2);
                numa += 2;
            }
#ifdef KVS
            printf("Numa %d \n", numa);
            args[i].recv_from = replica_cores[numa];
#endif
        }
    
        node = smlt_get_node_by_id(cores[i]);
        err = smlt_node_start(node, client_function, (void*) &args[i]);
        if (smlt_err_is_fail(err)) {
            printf("Staring node failed \n");
        }
     
       
    }
}
