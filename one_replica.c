/**
 * \file
 * \brief Onepaxos replica implementation
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
#include <smlt.h>
#include <smlt_broadcast.h>
#include <smlt_context.h>
#include <smlt_message.h>
#include <smlt_topology.h>

#include <sys/time.h>

#include "crc.h"
#include "incremental_stats.h"
#include "internal_com_layer.h"
#include "consensus.h"
#include "one_replica.h"
#include "client.h"
#include "flags.h"

#define MAX_BACKOFF 150
#define LEADER_TIMEOUT 350
#ifdef VERIFY
#define TIMEOUT 1000
#else
#define TIMEOUT 350
#endif

#define ONE_PREP 3
#define ONE_PREP_RESP 4
#define ONE_ACC 5
#define ONE_LEARN 6
#define ONE_IS_ALIVE 7
#define ONE_CHANGE_LEADER 8
#define ONE_IS_LEADER 9
#define ONE_GET_ACCEPTOR 10
#define ONE_ABAN 11
#define ONE_CHANGE_ACCEPTOR 12
#define ONE_VERIFY 15


/*
 * Format of messages sent around
 * The is_alive as well as
 * the change leader/acceptor messages
 * use the index field to signify request/response
 * and the n field for the answer
 */

struct entry{
    struct smlt_msg* msg;
	struct entry* next;
};

typedef struct entry_queue_t {
	uint64_t size;
	struct entry* head;
	struct entry* tail;
} entry_queue_t;

typedef struct onepaxos_replica_t{
	uint8_t id;
    uint8_t current_core;
	uint8_t num_clients;
	uint8_t num_replicas;
	uint64_t num_requests;
	void (*exec_fn) (void *);

	// connection setup state
    uint8_t* clients;
    uint8_t* replicas;

	// state of the replica for protocol
	uint8_t current_leader;
	uint8_t current_acceptor;
	bool leader_timeout;
	bool acceptor_timeout;
	bool change;
	bool voted;
	bool* is_dead;
	uint16_t backoff;

	struct entry* proposals;
	struct entry* chosen;

	uint64_t proposal_index;
	uint64_t index;
	uint64_t current_n;
	uint64_t highest_proposal_number;
	uint64_t last_executed_rid[MAX_NUM_CLIENTS];

	// acceptor/leader change state
	uint16_t num_success;
	uint16_t current_acceptor_votes[MAX_NUM_REPLICAS];

	// leader state
	uint64_t last_index[MAX_NUM_CLIENTS];

	entry_queue_t entry_queue;
	bool entry_q_initialized;
	struct entry last_entry;

	// composition information
    char* prog_string;
	uint8_t level;
	uint8_t alg_below;
	uint8_t node_size;
	uint8_t started_from_id;
	uint8_t *cores;

} onepaxos_replica_t;

static __thread onepaxos_replica_t replica;
extern struct smlt_context* ctx;
extern struct smlt_topology* topo;

#ifdef VERIFY
uint32_t* crcs;
uint32_t crc_count;
uint64_t* rid;
uint32_t* cid;
#endif

static uint64_t tsc_per_ms;
// periodic events to check if acceptor/leader is alive TODO


// prototypes
static void handle_setup(struct smlt_msg* msg);
static void handle_request(struct smlt_msg* msg);
static void handle_prepare(struct smlt_msg* msg);
static void handle_prepare_response(struct smlt_msg* msg);
static void handle_accept(struct smlt_msg* msg);
static void handle_learn(struct smlt_msg* msg);
static void handle_abandon(struct smlt_msg* msg);
static void handle_is_current_leader(struct smlt_msg* msg);
static void handle_is_current_leader_response(struct smlt_msg* msg);
static void handle_get_current_acceptor(struct smlt_msg* msg);
static void handle_get_current_acceptor_response(struct smlt_msg* msg);
static void handle_is_alive(struct smlt_msg* msg);
static void handle_is_alive_response(struct smlt_msg* msg);
static void handle_change_leader(struct smlt_msg* msg);
static void handle_change_acceptor(struct smlt_msg* msg);

static bool execute(uintptr_t* msg);
static uint16_t next_acceptor_id(void);


// Throughput mesaurement
#ifdef MEASURE_TP
static bool timer_started = false;
static uint64_t total_start;
static uint64_t total_end;

static uint64_t num_reqs = 0;
static uint8_t runs = 0;
static double run_res[7];
static incr_stats avg;

static void print_results_one(onepaxos_replica_t* rep) {

    init_stats(&avg);
    char* f_name = (char*) malloc(sizeof(char)*100);
#ifdef SMLT
    sprintf(f_name, "results/tp_%s_one_num_%d_numc_%d", "adaptivetree",
            rep->num_replicas, rep->num_clients);
#else
    sprintf(f_name, "results/tp_one_below_%d_num_%d_numc_%d",
            rep->alg_below, rep->num_replicas, rep->num_clients);
#endif
#ifndef BARRELFISH
    FILE* f = fopen(f_name, "a");
#endif
    RESULT_PRINTF(f, "#####################################################");
    RESULT_PRINTF(f, "#####################\n");
    RESULT_PRINTF(f, "algo_below %d num_clients %d topo %s \n", rep->alg_below,
            rep->num_clients, "adaptivetree");
    for (int i = 2; i < 6; i++) {
        RESULT_PRINTF(f, "%10.3f \n", run_res[i]);
        add(&avg, run_res[i]);
    }

    RESULT_PRINTF(f, "avg %10.3f, stdv %10.3f, 95%% conf %10.3f\n",
            get_avg(&avg), get_std_dev(&avg), get_conf_interval(&avg));

    RESULT_PRINTF(f, "||\t%10.3f\t%10.3f\t%10.3f\n",
            get_avg(&avg), get_std_dev(&avg), get_conf_interval(&avg));

#ifndef BARRELFISH
    fflush(f);
    fclose(f);
#endif
}

static void* results_one(void* arg)
{
    onepaxos_replica_t* rep = (onepaxos_replica_t*) arg;
    while (true){
        total_end = rdtsc();
#ifdef BARRELFISH
        printf("Replica %d : Throughput/s current %10.6g \n",
                        disp_get_core_id(), (double) num_reqs/20);
#else
        printf("Replica %d : Throughput/s current %10.6g \n",
                sched_getcpu(), (double) num_reqs/20);
#endif
        run_res[runs] = (double) num_reqs/20;
        // reset stats
        num_reqs = 0;
        total_start = rdtsc();
        runs++;
        if (runs > 6){
            print_results_one(rep);
            break;
        }
        sleep(20);
    }
    return 0;
}
#endif

/*
 * Message handler
 */
static void message_handler_onepaxos(struct smlt_msg *msg)
{
	switch (get_tag(msg->data)) {
	    case SETUP_TAG:
            handle_setup(msg);
            break;
	    case REQ_TAG:
            //printf("Replica %d: handle request \n", sched_getcpu());
            handle_request(msg);
            break;
	    case ONE_PREP:
            handle_prepare(msg);
	        break;

	    case ONE_PREP_RESP:
            handle_prepare_response(msg);
	        break;

	    case ONE_ACC:
            //printf("Replica %d: handle acc \n", sched_getcpu());
            handle_accept(msg);
	        break;

	    case ONE_LEARN:
            //printf("Replica %d: handle learn \n", sched_getcpu());
            handle_learn(msg);
	        break;

	    case ONE_ABAN:
            handle_abandon(msg);
	        break;

	    case ONE_CHANGE_LEADER:
            handle_change_leader(msg);
	        break;

	    case ONE_CHANGE_ACCEPTOR:
            handle_change_acceptor(msg);
	        break;

	    case ONE_IS_LEADER:
            if (msg->data[1] == 0) {
                handle_is_current_leader(msg);
            } else {
                handle_is_current_leader_response(msg);
            }
	        break;

	    case ONE_GET_ACCEPTOR:
            if (msg->data[1] == 0) {
                handle_get_current_acceptor(msg);
            } else {
                handle_get_current_acceptor_response(msg);
            }
	        break;

	    case ONE_IS_ALIVE:
            if (msg->data[1] == 0) {
                handle_is_alive(msg);
            } else {
                handle_is_alive_response(msg);
            }
	        break;
#ifdef VERIFY
	    case ONE_VERIFY:
            handle_verify(msg);
	        break;
#endif
	    default:
		    printf("Replica %d: unknown message type %" PRIu16 " \n", 
                    replica.current_core, get_tag(msg->data));
	}
}


#ifdef SMLT
__thread uintptr_t msg2[7];
void message_handler_loop_onepaxos(void)
{
    errval_t err;
    struct smlt_msg* message = smlt_message_alloc(56);
    if (replica.id == replica.current_leader) {
        uint64_t* cores = (uint64_t*) malloc(sizeof(uint64_t)*
                                             replica.num_clients*2);
        for (int i = 0; i < replica.num_clients; i++) {
            cores[i] = replica.clients[i];
        }

        struct smlt_topology_node* parent;
        struct smlt_topology_node* node;
        node = smlt_topology_node_get_by_id(topo, replica.current_core);
        parent = smlt_topology_node_get_parent(node);
        for (int i = replica.num_clients; i < replica.num_clients*2; i++) {
            cores[replica.num_clients] = smlt_topology_node_get_id(node);
        }

        int num_cores = 2*replica.num_clients;
        while (true) {
            for (int i = 0; i < num_cores; i++) {
                if (smlt_can_recv(cores[i])) {
                    err = smlt_recv(cores[i], message);
                    if (cores[i] != (uint64_t) smlt_topology_node_get_id(node)) {
                        message_handler_onepaxos(message);
                    } else {
                        smlt_broadcast(ctx, message);
                        message_handler_onepaxos(message);
                    }
                }
            }
        }
    } else if (replica.id == replica.current_acceptor) {
        while (true) {
            if (smlt_can_recv(replica.replicas[replica.current_leader])) {
                smlt_recv(replica.replicas[replica.current_leader], message);
                message_handler_onepaxos(message);
            }
        }

    } else {
        while (true) {
            smlt_broadcast(ctx, message)
            message_handler_onepaxos(message);
            if (message->data[3] == replica.current_core) {
                set_tag(message->data, RESP_TAG);
                smlt_send(replica.clients[get_client_id(message->data)], message);
            }
        }
    }
}
#else
void message_handler_loop_onepaxos(void)
{
    errval_t err;
    struct smlt_msg* message = smlt_message_alloc(56);
    if (replica.id == replica.current_leader) {
        int j = 0;
        uint8_t* all_cores = (uint8_t*) malloc(sizeof(uint8_t)* (replica.num_replicas +
                                replica.num_clients));
        for (int i = 0; i < replica.num_replicas; i++) {
            all_cores[i] = replica.replicas[i];
        }

        for (int i = replica.num_replicas; i < (replica.num_replicas+ replica.num_clients); i++) {
            all_cores[i] = replica.clients[(i-replica.num_replicas)];
        }

        while (true) {
            if (smlt_can_recv(all_cores[j])) {
                err = smlt_recv(all_cores[j], message);
                if (smlt_err_is_fail(err)) {
                    // TODO
                }
                message_handler_onepaxos(message);
            }
            j++;

            j = j % (replica.num_replicas + replica.num_clients);
        }

    } else if (replica.id == replica.current_acceptor) {
        while (true) {
            if (smlt_can_recv(replica.replicas[replica.current_leader])) {
                err = smlt_recv(replica.replicas[replica.current_leader], message);
                if (smlt_err_is_fail(err)) {
                    // TODO
                }
                message_handler_onepaxos(message);
            }
        }

    }else {

        while (true) {
            if (smlt_can_recv(replica.replicas[replica.current_acceptor])) {
                smlt_recv(replica.replicas[replica.current_acceptor], message);
                if (smlt_err_is_fail(err)) {
                    // TODO
                }
                message_handler_onepaxos(message);

#ifdef KVS
                if (message->data[3] == replica.current_core) {
                    set_tag(message->data, RESP_TAG);
                    err = smlt_send(replica.clients[get_client_id(message->data)], 
                                    message);
                    if (smlt_err_is_fail(err)) {
                        // TODO
                    }
                }
#endif
            }
        }
    }
}
#endif

static void entry_queue_init(entry_queue_t *st)
{
	st->size = 0;
	st->head = st->tail = NULL;
}

static bool entry_q_empty(entry_queue_t *client) {
	return client->head == NULL;
}

static void entry_q_enqueue(struct entry *ele)
{
	entry_queue_t *client = &replica.entry_queue;

	ele->next = NULL;

	if (client->head == NULL) {
	    client->head = client->tail = ele;
	    client->size++;
	} else {
	    client->tail->next = ele;
	    client->tail = ele;
	    client->size++;
	}

}

static struct entry *entry_q_dequeue(void)
{
	entry_queue_t *client = &replica.entry_queue;
	if (client->head == NULL) {
	   return NULL;
	} else {
	   struct entry *ele = client->head;
	   client->head = ele->next;
	   if (client->head == NULL) {
	       client->tail = NULL;
	   }
	   client->size--;
	   return ele;
	}
}

/*
 * Functions to be peridocally called
 */
/*
uint32_t num_iter = 1;
static void check_acceptor_alive_cb(void *arg)
{
    if (replica.acceptor_timeout && !replica.change) {
    	printf("Replica %d: assume acceptor dead \n", replica.id);
	    multicast_is_current_leader(replica.id);
	    replica.change = true;
    }

    if (replica.current_acceptor < replica.num_replicas) {
     	struct onepaxos_binding* b = replica.replica_bindings[replica.current_acceptor];
     	b->tx_vtbl.is_alive(b, NOP_CONT);
     	replica.acceptor_timeout = true;
    }
#ifdef MEASURE_TP
    num_iter++;
    if ((num_iter % num_iter_before_show) == 0) {
	    show_results();
    }
#endif
}

static void check_leader_alive_cb(void *arg)
{
    if (replica.leader_timeout && !replica.change) {
	    printf("Replica %d: Trying to become leader \n", replica.id);
	    multicast_get_current_acceptor();
	    replica.change = true;
    }

    if (replica.current_leader < replica.num_replicas) {
        struct onepaxos_binding* b = replica.replica_bindings[replica.current_leader];
        b->tx_vtbl.is_alive(b, NOP_CONT);
        replica.leader_timeout = true;
    }
}
*/

/*
 * Handler Methods
 */

#ifdef VERIFY
static void handle_verify(uintptr_t* msg)
{
	crcs[get_client_id(msg)] = msg[4];
	crc_count++;
#if defined(ACCEPTOR_FAIL_1) || defined(ACCEPTOR_FAIL_2) || c cdefined(LEADER_FAIL_1)
	if (crc_count == (replica.num_replicas-2)) {
#else
	if (crc_count == (replica.num_replicas-1)) {
#endif
        int num_wrong = 0;
	    for (int i = 0; i < replica.num_replicas; i++) {
	        // received a crc
		    if (crcs[i] != 0) {
		        printf("crcs[%d] %d and crcs[%d] %d \n", replica.current_leader,
                                        crcs[replica.current_leader], i, crcs[i]);
		        if (crcs[replica.current_leader] != crcs[i]) {
		            num_wrong++;
		        }
		    }
	    }

        if (num_wrong > 0) {
		    printf("Implementation not correct \n");
	    } else {
		    printf("Implementation correct \n");
	    }
	}
}
#endif

static void handle_setup(struct smlt_msg* msg)
{
    errval_t err;
    uintptr_t core = get_client_id(msg->data);
    for (int i = 0; i < replica.num_clients; i++) {
        if (replica.clients[i] == core) {
            msg->data[4] = i;
        }
    }

    err = smlt_send(core, msg);
    if (smlt_err_is_fail(err)) {
        // TODO
    }   
}

static void handle_request(struct smlt_msg* msg)
{
    errval_t err;
#ifdef LEADER_FAIL_1
    if ((replica.proposal_index == (replica.num_requests/2))
	     && replica.id == 0) {
	    printf("Replica %d: leader failed \n", replica.id);
	    while(true){ };
    }
#endif

#ifdef MEASURE_TP
    if (!timer_started){
        total_start = rdtsc();
        timer_started = true;
    }
#endif
    if (replica.id == replica.current_leader){
        struct entry* ele = (struct entry*) malloc(sizeof(struct entry));
        ele->msg = msg;
        entry_q_enqueue(ele);
        set_tag(msg->data, ONE_ACC);
        err = smlt_send(replica.replicas[replica.current_acceptor], msg);
        if (smlt_err_is_fail(err)) {
            // TODO
        }

        replica.proposal_index++;

    } else {
        printf("Core %d: Forward to %d \n", replica.current_core,
               replica.replicas[replica.current_leader]);
        err = smlt_send(replica.replicas[replica.current_leader], msg);
        if (smlt_err_is_fail(err)) {
            // TODO
        }
    }
}

static void handle_prepare(struct smlt_msg* msg)
{
    errval_t err;
    if (replica.highest_proposal_number < msg->data[2]) {
    	replica.highest_proposal_number = msg->data[2];
    	// send to leader that I'm alive to reset timer
        // TODO SEND prepare response
        set_tag(msg->data, ONE_PREP_RESP);
        err = smlt_send(replica.current_leader, msg);
        if (smlt_err_is_fail(err)) {
            // TODO
        }

        // TODO SEND alive to leader
        set_tag(msg->data, ONE_IS_ALIVE);
        err = smlt_send(replica.current_leader, msg);
        if (smlt_err_is_fail(err)) {
            // TODO
        }

    } else {

    }

}

static void handle_prepare_response(struct smlt_msg* msg)
{
    errval_t err;
#ifdef DEBUG_FAIL
    printf("Replica %d: received prepare response %"PRIu64" index \n", 
           replica.id, msg->data[1]);
#endif
    // resend difference between proposals and index -> got some client requests before
    // acceptor died

    bool proposals_was_null = false;
    if (replica.entry_q_initialized == false) {
	    proposals_was_null = true;
        entry_queue_init(&replica.entry_queue);
	    replica.entry_q_initialized = true;

        replica.proposal_index = replica.index;
	    // just to be sure resend last entry
	    struct entry* ele1 = (struct entry*) malloc(sizeof(struct entry));
        ele1->msg = replica.last_entry.msg;
	    entry_q_enqueue(ele1);

        // TODO cancel periodic function call
 	   // periodic_event_cancel(&leader_alive);

        // TODO create new peridoic function call
	    //err = periodic_event_create(&acceptor_alive, get_default_waitset(), TIMEOUT*1000,
    }

    replica.change = false;
    if (!proposals_was_null) {
        while (!entry_q_empty(&replica.entry_queue)) {
            struct entry* ele = entry_q_dequeue();
            set_tag(ele->msg->data, ONE_ACC);
            err = smlt_send(replica.current_leader, msg);
            if (smlt_err_is_fail(err)) {
                // TODO
            }
            free(ele);
        }
    }

    if (proposals_was_null) {
    	// send leader change to clients
        // TODO SEND CHANGE OF LEADER
    }
}

static void handle_accept(struct smlt_msg* msg)
{
    errval_t err;
#ifdef ACCEPTOR_FAIL_1
    if ((replica.id == 1) && (msg->data[1] == (replica.num_requests/2))) {
        printf("Replica %d: Acceptor failing \n", replica.id);
        while(true);
    }
#endif

    if (msg->data[2] >= replica.highest_proposal_number){
	    // for the acceptor the value is already chosen
	    replica.last_entry.msg = msg;
#ifdef VERIFY
	    rid[index] = get_request_id(msg->data);
	    cid[index] = get_client_id(msg->data);
#endif

        // broadcast learn
        set_tag(msg->data, ONE_LEARN);
        msg->data[1] = replica.index;
#ifdef SMLT
        err = smlt_broadcast(ctx, msg);
        if (smlt_err_is_fail(err)) {
            // TODO
        }
#else
        for (int i = 0; i < replica.num_replicas; i++) {
            if (i == 1) {
                continue;
            }

            err = smlt_send(replica.replicas[i], msg);
            if (smlt_err_is_fail(err)) {
                // TODO
            }
        }
#endif
	    execute(msg->data);

        if (replica.alg_below != ALG_NONE) {
	        com_layer_core_send_request(msg);
        }

        if (replica.index != msg->data[1]){
	        // Same index twice -> some other server may be down
	        replica.index = msg->data[1];
        }

	    replica.index++;

#ifdef VERIFY
        if (replica.index == (replica.num_requests)) {
	        crc_t crc;
	        crc = crc_init();
	        crc = crc_update(crc, (const unsigned char *) rid, sizeof(uint64_t)*replica.num_requests);
	        crc = crc_update(crc, (const unsigned char *) cid, sizeof(uint16_t)*replica.num_requests);
	        crc = crc_finalize(crc);
	        if (replica.id == replica.current_leader) {
		        crcs[replica.current_leader] = crc;
	        } else {

                // TODO SEND VERIFY to leadera
                set_tag(msg->data, ONE_VERIFY);
                set_client_id(msg->data, replica.id);
                msg->data[4] = crc;
		        printf("Replica %d: sent verify \n", replica.id);
	        }
	    }
#endif

    } else {
        printf("Replica %d: in accept sending abandon \n", replica.id);
        // TODO SEND ABANDONE
    }
}

static void handle_learn(struct smlt_msg* msg)
{
    errval_t err;
#ifdef MEASURE_TP
    if (!timer_started){
       total_start = rdtsc();
       timer_started = true;
    }
#endif

    replica.voted = false;
    replica.change = false;
#ifdef VERIFY
    rid[msg->data[1]] = get_request_id(msg);
    cid[msg->data[1]] = get_client_id(msg);
#endif

    bool success = execute(msg->data);

    if (replica.alg_below != ALG_NONE) {
	    com_layer_core_send_request(msg);
    }

    if (replica.index != msg->data[1]){
	    // Same index twice -> some other server may be down
	    replica.index = msg->data[1];
    }

    replica.index++;

    if (replica.id == replica.current_leader) {
	    if (replica.entry_queue.size > ((replica.proposal_index-replica.index)+1)){
	        struct entry* ele = entry_q_dequeue();
            free(ele);
	    }
	    // was no duplica i.e. not yet replied to
	    if (success) {
	        // don't care for reply on core level
	        if (replica.level == NODE_LEVEL) {
#ifndef KVS
                set_tag(msg->data, RESP_TAG);
                err = smlt_send(replica.clients[get_client_id(msg->data)], msg);
                if (smlt_err_is_fail(err)) {
                    // TODO
                }
#endif
 	        } else {
                set_tag(msg->data, RESP_TAG);
                err = smlt_send(replica.started_from_id, msg);
                if (smlt_err_is_fail(err)) {
                    // TODO
                }
	        }
#ifdef MEASURE_TP
	        num_reqs++;
#endif
	    }
    }

#ifdef VERIFY
    if (replica.index == (replica.num_requests)) {
	    crc_t crc;
	    crc = crc_init();
	    crc = crc_update(crc, (const unsigned char *) rid, sizeof(uint64_t)*replica.num_requests);
	    crc = crc_update(crc, (const unsigned char *) cid, sizeof(uint16_t)*replica.num_requests);
	    crc = crc_finalize(crc);
	    if (replica.id == replica.current_leader) {
	    	crcs[replica.current_leader] = crc;
	    } else {

            set_tag(msg->data, ONE_VERIFY);
            set_client_id(msg->data, replica.id);
            msg->data[4] = crc;
            //msg->req.tag = ONE_VERIFY;
            //msg->req.client_id = replica.id;
		    printf("Replica %d: sent verify \n", replica.id);
	    }
	}
#endif

}

static void handle_abandon(struct smlt_msg* msg)
{
    // still leader
    if (get_client_id(msg->data) == replica.id) {
	    return;
    }

    if (replica.id != replica.current_leader) {
	    return;
    }

    // TODO not sure if useful ?


}

/*
 * leader election stuff
 */
/*
 *  msg->n the answer to the request i.e. is it the leader
 *  msg->index == 1 means it is a response
 */
static void handle_is_current_leader(struct smlt_msg* msg)
{
#ifdef DEBUG_FAIL
     printf("Replica %d: received is current leader \n", replica.id);
#endif
    if (get_client_id(msg->data) == replica.current_leader) {
        msg->data[1] = 1;
        msg->data[2] = 1;
        // TODO send response
    } else {
        msg->data[1] = 1;
        msg->data[2] = 0;
        // TODO SEND response
    }
}


/*
 *  msg->n the answer i.e. the current acceptor
 *  msg->index == 1 means it is a response
 */
static void handle_get_current_acceptor(struct smlt_msg* msg)
{
    if (!replica.voted) {
        msg->data[1] = 1;
        msg->data[2] = replica.current_acceptor;
        set_client_id(msg->data, replica.id);
	    replica.voted = true;
	    replica.change = true;
#ifdef DEBUG_FAIL
        printf("Replica %d: received get current acceptor, gave vote \n", replica.id);
#endif
	    return;
    }
#ifdef DEBUG_FAIL
    printf("Replica %d: received get current acceptor rejected \n", replica.id);
#endif
}

static void handle_is_current_leader_response(struct smlt_msg* msg)
{
     // dont need myself for majority since already voted for myself
    int majority;
    if ((replica.num_replicas % 2) == 0) {
        majority = ((replica.num_replicas-1)/2)+1;
    } else {
        majority = ((replica.num_replicas-1)/2);
    }

    if ((msg->data[2] == 1)) {
    	replica.num_success++;
    	// reached a majority
	    if (replica.num_success == majority) {
	        // Im currently the leader
	        replica.is_dead[replica.current_acceptor] = true;
	        replica.current_acceptor = next_acceptor_id();
	        if (replica.current_acceptor == 65535) {
	            printf("Replica %d: not enough nodes left to get new acceptor \n", replica.id);
	            return;
	        }
#ifdef DEBUG_FAIL
            printf("Replica %d: multicast acceptor change \n", replica.id);
#endif
	        replica.current_n++;
            set_tag(msg->data, ONE_CHANGE_ACCEPTOR);
            msg->data[2] = replica.current_acceptor;
            // TODO BROADCAST change key fig

        	replica.num_success = 0;

	        printf("Replica %d: sending prepare to %d \n", replica.id, replica.current_acceptor);
            // TODO SEND replicas.last_entry to accetpor
            set_tag(msg->data, ONE_ACC);
        }
    }
}

static void handle_get_current_acceptor_response(struct smlt_msg* msg)
{
#ifdef DEBUG_FAIL
    printf("Replica %d: get current acceptor response \n", replica.id);
#endif
    int majority;
    if ((replica.num_replicas % 2) == 0) {
        majority = ((replica.num_replicas-1)/2)+1;
    } else {
        majority = ((replica.num_replicas-1)/2);
    }
    // reached a majority
    replica.current_acceptor_votes[msg->data[2]]++;
    if (replica.current_acceptor_votes[msg->data[2]] == majority) {
	    // announce leader change
	    replica.current_acceptor = msg->data[2];
	    replica.is_dead[replica.current_leader] = true;

	    replica.current_n++;

	    // announce leader change
	    replica.current_leader = replica.id;
        set_tag(msg->data, ONE_CHANGE_LEADER);
        msg->data[2] = replica.id;

	    printf("Replica %d: I'm the new leader \n", replica.id);
	    printf("Replica %d: sending prepare to %d \n", replica.id, replica.current_acceptor);

        // TODO SEND to new acceptor
        set_tag(msg->data, ONE_PREP);
        /*
	    send_prepare(replica.replica_bindings[replica.current_acceptor],
			replica.last_entry.client_id,
			replica.last_entry.request_id,
			replica.last_entry.index,
			replica.current_n,
			replica.last_entry.cmd);
        */
	    // rest state
	    for (int i = 0; i < replica.num_replicas; i++) {
	        replica.current_acceptor_votes[i] = 0;
	    }
    }
}

static void handle_change_leader(struct smlt_msg* msg)
{
#ifdef DEBUG_FAIL
           printf("Replica %d: received change key figure \n", replica.id);
#endif
    if (replica.id == replica.current_leader) {
        // TODO abort peridoic function
 	    //   periodic_event_cancel(&acceptor_alive);
	    // no longer leader
	}

	if (get_client_id(msg->data) != replica.current_leader) {
	    // new leader
    	replica.is_dead[replica.current_leader] = true;
	    replica.current_leader = get_client_id(msg->data);
	    replica.leader_timeout = false;
	} else {
	        // acceptor changed TODO
	}
#ifdef DEBUG_FAIL
    printf("Replica %d: current acceptor %d current leader %d \n", replica.id,
		   replica.current_acceptor, replica.current_leader);
#endif
}

static void handle_change_acceptor(struct smlt_msg* msg)
{
    if (get_client_id(msg->data) == replica.current_leader) {
        // acceptor changed by leader
	    replica.is_dead[replica.current_acceptor] = true;
	    replica.current_acceptor = msg->data[2];
	    replica.acceptor_timeout = false;
	} else {
	        // leader change TODO
    }
}

static void handle_is_alive(struct smlt_msg* msg)
{
     msg->data[1] = 1;
     msg->data[2] = replica.id;
     // TODO SEND response
}


static void handle_is_alive_response(struct smlt_msg* msg)
{
	if (msg->data[2] == replica.current_leader) {
	   replica.leader_timeout = false;
	} else if (msg->data[2] == replica.current_acceptor) {
	   replica.acceptor_timeout = false;
	}
}

void set_execution_fn_onepaxos(void (*exec_fn)(void *))
{
     replica.exec_fn = exec_fn;
}

static void default_exec_fn(void* addr);
static void default_exec_fn(void* addr)
{
	return;
}

uint16_t get_cmd_size(void)
{
     return sizeof(3*sizeof(uint64_t));
}

void init_replica_onepaxos(uint8_t id,
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
                           void (*exec_fn)(void *))
{
	replica.id = id;
	replica.cores = cores;
    replica.current_core = current_core;
	// just set 0 as the leader at the beginning
#ifdef SMLT
	replica.current_leader = 1;
	replica.current_acceptor = 0;
#else
	replica.current_leader = 0;
	replica.current_acceptor = 1;
#endif
	replica.num_clients = num_clients;
	replica.num_replicas = num_replicas;
	replica.num_requests = num_requests;
    replica.clients = clients;
    replica.replicas = replicas;

	if (exec_fn == NULL) {
	   replica.exec_fn = &default_exec_fn;
	} else {
	   replica.exec_fn = exec_fn;
	}

	replica.node_size = node_size;
	replica.level = level;
	replica.alg_below = alg_below;
	replica.current_n = 1;
	replica.highest_proposal_number = 0;
	replica.index = 0;
	replica.proposal_index = 0;
	replica.num_success = 0;
	replica.started_from_id = started_from;
	replica.is_dead = (bool*) malloc(sizeof(bool)*num_replicas);
	// Zero would mean we find a duplica
	for (int i = 0; i < MAX_NUM_CLIENTS; i++) {
	    replica.last_executed_rid[i] = -1;
	}
	replica.leader_timeout = false;
	replica.acceptor_timeout = false;
	replica.change = false;
	replica.voted = false;
	replica.backoff = rdtsc() % MAX_BACKOFF;

	if (id == 0) {
	    entry_queue_init(&replica.entry_queue);
	    replica.entry_q_initialized = true;
	} else {
	    replica.entry_q_initialized = false;
	}


#ifdef VERIFY
	crcs = malloc(sizeof(uint32_t)*(num_replicas));
	rid = malloc(sizeof(uint64_t)*(num_requests+MAX_NUM_CLIENTS+2));
	cid = malloc(sizeof(uint64_t)*(num_requests+MAX_NUM_CLIENTS+2));
	crc_count = 0;
#endif

    // TODO GET frequency in cycles
	tsc_per_ms = 2400000;

    if (replica.alg_below != ALG_NONE) {
        com_layer_core_init(replica.alg_below, replica.id, replica.current_core,
                            replica.cores, replica.node_size,
                            get_cmd_size(), replica.exec_fn);
    }

    // TODO Start periodic functons
    if (id == 0 && (level != CORE_LEVEL)) {
	   // err = periodic_event_create(&acceptor_alive, get_default_waitset(),
       //                               TIMEOUT*1000,
	}

	if ((id != 1) && (id != 0) && (level != CORE_LEVEL)) {
	    //err = periodic_event_create(&leader_alive, get_default_waitset(),
        //(TIMEOUT+replica.backoff)*1000,
	}

#ifdef MEASURE_TP
    if ((id == 0) && (level == NODE_LEVEL)) {
       pthread_t tid;
       pthread_create(&tid, NULL, results_one, &replica);
    }
#endif
}

// assume all id's lower than the current acceptor failed
// or are otherwise leader etc.
static uint16_t next_acceptor_id(void){
	uint16_t result = -1;
	// next lowest ID that is not leader or old acceptor
	for (int i = (replica.current_acceptor+1); i < replica.num_replicas; i++) {
	    if (replica.current_leader != i) {
		    return i;
	    }
	}
	return result;
}

static bool execute(uintptr_t* msg)
{
	// was repetitive
	if ((replica.last_executed_rid[get_client_id(msg)] == get_request_id(msg)) ||
	    ((replica.last_executed_rid[get_client_id(msg)] > get_request_id(msg)) &&
	      !(replica.last_executed_rid[get_client_id(msg)] == (uint64_t)-1))) {

	   return false;
	}

	replica.exec_fn(&msg[4]);
	replica.last_executed_rid[get_client_id(msg)] = get_request_id(msg);

	return true;
}
