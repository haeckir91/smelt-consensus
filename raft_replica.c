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


#include <sys/time.h>

#include "incremental_stats.h"
#include "client.h"
#include "internal_com_layer.h"
#include "consensus.h"
#include "raft_replica.h"
#include "flags.h"
#include "mp.h"
#include "sync.h"

#define RAFT_APP 3
#define RAFT_APPR 4
#define RAFT_APPE 5
#define RAFT_REQV 6
#define RAFT_REQVR 7

#define HEARTBEAT_TIMEOUT 50
#define ELECTION_RESET_TIMEOUT 100
#define ELECTION_TIMEOUT  200
#define BACKOFF_MAX 150

struct log_queue{
    int size;
    struct log_entry* head;
    struct log_entry* tail;
};

struct log_entry{
    uint8_t exec_count;
    uint64_t index;
    uint64_t term;
    uintptr_t header;
    uintptr_t payload[3];
    struct log_entry* next;
    struct log_entry* prev;
};

typedef struct raft_replica_t{	
	uint8_t id;
	uint8_t num_clients;
	uint8_t num_replicas;
	uint8_t num_requests;	
    uint8_t current_core;
    void (*exec_fn) (void *);

	// connection setup state
    uint8_t* clients;
    uint8_t* replicas;

	// state of the replica for protocol
	uint64_t current_term;
	uint8_t current_leader;
	int voted_for;

	// log entries 
	uint64_t commit_index;
	uint64_t last_applied;
	uint64_t last_log_index;
    uint64_t previous_term;
    struct log_entry* previous_entry;

	// leader state
    struct log_queue queue;
	uint64_t next_index[MAX_NUM_REPLICAS];
	uint64_t match_index[MAX_NUM_REPLICAS];
	uint64_t last_client_request[MAX_NUM_CLIENTS];
	uint16_t num_appendEntry_replys[MAX_NUM_REPLICAS];	
	uint16_t num_requestVoted_replys[MAX_NUM_REPLICAS];	

	uint16_t num_votes;
	uint16_t num_rejects;

	// timers
	bool election_timeout;
	uint8_t backoff;

	bool is_leader;
	bool is_candidate;
	bool is_follower;
	
    // composition state
    uint8_t started_from;
    uint8_t alg_below;
    uint8_t level;
    uint8_t node_size;

    // measurement
    uint64_t num_reqs;
    incr_stats avg;
    double run_res[7];
    uint8_t runs;
} raft_replica_t;


static __thread raft_replica_t replica;

static void handle_setup(uintptr_t* msg);
static void handle_request(uintptr_t* msg);
static void handle_append(uintptr_t* msg);
static void handle_empty_append(uintptr_t* msg);
static void handle_append_response(uintptr_t* msg);
static void handle_vote(uintptr_t* msg);
static void handle_vote_response(uintptr_t* msg);

static void cleanup_queue(struct log_queue* queue);

/*
 * Measurement stuff
 */ 

#ifdef MEASURE_TP
static void print_results_raft(raft_replica_t* rep) {

    init_stats(&rep->avg);
    char* f_name = (char*) malloc(sizeof(char)*100);
    sprintf(f_name, "results/tp_raft_below_%d_num_%d_numc_%d",
            rep->alg_below, rep->num_replicas, rep->num_clients);
#ifndef BARRELFISH
    FILE* f = fopen(f_name, "a");
#endif
    RESULT_PRINTF(f, "#####################################################");
    RESULT_PRINTF(f, "#####################\n");
    RESULT_PRINTF(f, "algo_below %d num_clients %d \n", rep->alg_below,
            rep->num_clients);
    for (int i = 2; i < 6; i++) {
        RESULT_PRINTF(f, "%10.3f \n", rep->run_res[i]);
        add(&rep->avg, rep->run_res[i]);
    }

    RESULT_PRINTF(f, "avg %10.3f, stdv %10.3f, 95%% conf %10.3f\n",
            get_avg(&rep->avg), get_std_dev(&rep->avg), get_conf_interval(&rep->avg));

    RESULT_PRINTF(f, "||\t%10.3f\t%10.3f\t%10.3f\n",
            get_avg(&rep->avg), get_std_dev(&rep->avg), get_conf_interval(&rep->avg));

#ifndef BARRELFISH
    fflush(f);
    fclose(f);
#endif
}
static void* results_raft(void* arg)
{
    raft_replica_t* rep = (raft_replica_t*) arg;
    while (true){
#ifdef BARRELFISH
        printf("Replica %d : Throughput/s current %10.6g \n",
                        disp_get_core_id(), (double) rep->num_reqs/20);
#else
        printf("Replica %d : Throughput/s current %10.6g \n",
                sched_getcpu(), (double) rep->num_reqs/20);
#endif
        rep->run_res[rep->runs] = (double) rep->num_reqs/20;
        // reset stats
        rep->num_reqs = 0;
        rep->runs++;
        if (rep->runs > 6){
            print_results_raft(rep);
            break;
        }
        sleep(20);
    }
    return 0;
}
#endif


/*
 * Log queue operations.
 */
/*
static void print_queue_state(struct log_queue *queue);
static void print_queue_state(struct log_queue *queue)
{
    struct log_entry *ele = queue->head;
    while (ele != NULL) {
        
        printf("Replica %d: ele->index %" PRIu64" \n", 
                replica.current_core, ele->index);
        printf("Replica %d: ele->term %" PRIu64" \n",
                replica.current_core, ele->term);
        printf("Replica %d: ele->exec_count %d \n", 
                replica.current_core, ele->exec_count);
        ele = ele->next;
    }
}
*/
static void log_queue_init(struct log_queue *queue)
{
    queue->size = 0;
    queue->head = queue->tail = NULL;
}

static void enqueue(struct log_queue *queue, struct log_entry *ele) 
{
	ele->next = NULL;
	
	if (queue->head == NULL) {
	    queue->head = queue->tail = ele;
	    queue->size++;
	} else {
        struct log_entry* prev_t = queue->tail;
	    queue->tail->next = ele;
	    queue->tail = ele;
        queue->tail->prev = prev_t;
	    queue->size++;
	}

}

static struct log_entry* remove_entry(struct log_queue *queue,
                                   uint64_t index)
{
    struct log_entry *ele = queue->head;
    while (ele != NULL) {
        if ((ele->index == index)) {
            struct log_entry* prev;
            struct log_entry* next;
            prev = ele->prev;
            next = ele->next;
            prev->next = next;
            next->prev = prev;
            return ele;
        }
        ele = ele->next;
    }
    return NULL;

}


static struct log_entry* queue_contains(struct log_queue *queue, 
                                        uint64_t index)
{
    struct log_entry *ele = queue->head;
    while (ele != NULL) {
        if ((ele->index == index)) {
            return ele;
        }
        ele = ele->next;
    }
    return NULL;
}


static void handle_setup(uintptr_t* msg)
{
    uintptr_t core = get_client_id(msg);
    for (int i = 0; i < replica.num_clients; i++) {
        if (replica.clients[i] == core) {
            msg[4] = i;
        }
    }

    mp_send7(core,
             msg[0], msg[1], msg[2], msg[3], msg[4],
             msg[5], msg[6]);
}

static void message_handler_raft(uintptr_t *msg) 
{
    switch (get_tag(msg)) {
        case SETUP_TAG: 
            handle_setup(msg);
            break;
             
	    case REQ_TAG:
            handle_request(msg);
	        break; 
		
	    case RAFT_APP:
            handle_append(msg);
	        break; 

	    case RAFT_APPE:
            handle_empty_append(msg);
	        break; 

	    case RAFT_APPR:
            handle_append_response(msg);
	        break; 

	    case RAFT_REQV:
            handle_vote(msg);
	        break; 

	    case RAFT_REQVR:
            handle_vote_response(msg);
	        break; 
	    default:
		    printf("Replica %d: unknown type in queue %" PRIu16" \n", 
                    replica.current_core, get_tag(msg));
	}
}

void message_handler_loop_raft(void)
{
    uintptr_t* message = (uintptr_t*) malloc(sizeof(uintptr_t)*8);
    if (replica.id == replica.current_leader) {
        int j = 0;
        uint8_t* all_cores = (uint8_t*) malloc(sizeof(uint8_t)* (replica.num_replicas +
                                replica.num_clients));
        for (int i = 0; i < replica.num_replicas; i++) {
            all_cores[i] = replica.replicas[i];
        }

        for (int i = replica.num_replicas; i < 
             (replica.num_replicas+ replica.num_clients); i++) {
            all_cores[i] = replica.clients[(i-replica.num_replicas)];
        }

        while (true) {
            if (mp_can_receive(all_cores[j])) {
                mp_receive7(all_cores[j], message);
                message_handler_raft(message);
            }
            j++;

            j = j % (replica.num_replicas + replica.num_clients);
        }
    }else {
        while (true) {
            if (mp_can_receive(replica.replicas[replica.current_leader])) {
                mp_receive7(replica.replicas[replica.current_leader], message);
                message_handler_raft(message);
            }
        }
    }
}

// TODO apply to "State Machine"
/*
 * State udpate Methods
 */ 
static void execute(uintptr_t* msg)
{
    replica.exec_fn(msg);
}

static void update_state(uint64_t term, uint16_t leader_id) 
{
    if ((term > replica.current_term) || replica.is_candidate) {
       replica.current_term = term;
       replica.current_leader = leader_id;
       replica.voted_for = -1;
       replica.is_leader = false;
       replica.is_follower = true;
       replica.is_candidate = false;
    }
}


static void update_applied_entries(void) {

    while (replica.commit_index > replica.last_applied) {
	    // apply entry one at the time
	    replica.last_applied++;

        // TODO APPLY ENTRY
        struct log_entry* ele;
        ele = queue_contains(&replica.queue, replica.last_applied);
    
        execute(ele->payload);

	    // respond to client if I am the leader
	    if (replica.id == replica.current_leader) {
  	        // find client which sent this request
            mp_send7(replica.clients[get_client_id(&(ele->header))],
                     0,0,0,0,0,0,0);
            ele->exec_count++;
            cleanup_queue(&replica.queue);
#ifdef MEASURE_TP
            replica.num_reqs++;
#endif
	    } else {
            if (replica.last_applied > 2) {
                ele = remove_entry(&replica.queue, replica.last_applied-2);
                free(ele);
            }
        }
    }	
}


/*
 * Find N such that N > replica.commit_index and a majority
 * of replica.match_index[] >= N and replica.term[] == replica.current_term
 */ 
static void update_commit_index_leader(void)
{
	// majority of replicas TODO replica.num_replicas -1 since i do not follow
	// with the match_index of the leader itself
	
	int majority = (replica.num_replicas-1)/2;
	uint64_t to_test = 0;
	int num_larger = 0;	

	for (int i = 0; i < replica.num_replicas; i++) {
	    to_test = replica.match_index[i];
	    
	    num_larger = 0;
	    if (to_test > replica.commit_index) {
            for (int j = 0; j < replica.num_replicas; j++) {	  
	            if (to_test <= replica.match_index[j]) {
		            num_larger++;
		        }
		        if (num_larger >= majority) {
		            replica.commit_index = to_test;			
		        }
	        }
	    }
	}
}

static void cleanup_queue(struct log_queue* queue) 
{
    struct log_entry* ele = queue->head;
    while(ele != NULL) {
        // check received count and clean up
        if ((ele->exec_count == replica.num_replicas) &&
            (ele->index < replica.commit_index-1)) {
            // remove entry and free it
            struct log_entry* prev;
            struct log_entry* next;
            prev = ele->prev;
            next = ele->next;
            if (prev != NULL) {
                prev->next = next;
            }
        
            if (next != NULL) {
                next->prev = prev;
            }
            free(ele);
        }
        ele = ele->next;
    }    
}

/*
 * Handler Methods
 */
static __thread uintptr_t buf[8];
static void handle_request(uintptr_t* msg) 
{
    if (replica.id == replica.current_leader) {
       replica.last_log_index++;
       struct log_entry* ele = (struct log_entry*) malloc(sizeof(struct log_entry));
       ele->payload[0] = msg[4];
       ele->payload[1] = msg[5];
       ele->payload[2] = msg[6];
       ele->header = msg[0];
       ele->index = replica.last_log_index;
       ele->term = replica.current_term;       
       ele->exec_count = 0;

       enqueue(&replica.queue, ele);

       set_tag(&msg[0], RAFT_APP);
       for (int i = 0; i < replica.num_replicas; i++) {
           // combine current term and current leader
           if (i == replica.current_leader) {
              continue;
           }
           set_request_id(&msg[1], replica.current_term);
           set_tag(&msg[1], replica.current_leader);
           mp_send7(replica.replicas[i],
                    msg[0],
                    msg[1], 
                    replica.last_log_index-1,
                    replica.commit_index,
                    msg[4],
                    msg[5],
                    msg[6]);
       }

       for (int i = 0; i < replica.num_replicas; i++) {
           // combine current term and current leader
           if (i == replica.current_leader) {
              continue;
           }
           mp_receive7(replica.replicas[i],buf);
           message_handler_raft(buf);
       }

       replica.previous_term = replica.current_term;
    } else {
        // forward
        mp_send7(replica.replicas[replica.current_leader], 
                 msg[0], msg[1], msg[2], msg[3],
                 msg[4], msg[5], msg[6]);
    }
}

static void handle_empty_append(uintptr_t* msg)
{
    if (msg[1] >= replica.commit_index) {
	    replica.commit_index = MIN(msg[1], replica.last_log_index);
     	update_applied_entries();
    }
    replica.election_timeout = false;
}


static void handle_append(uintptr_t* msg) 
{
    // reset timer
    //printf("Repica %d: handle append \n", replica.id);
    replica.election_timeout = false;	
    uint32_t term = get_request_id(&msg[1]);
    uint8_t leader = get_tag(&msg[1]);
    uintptr_t prev_index = msg[2];
    uintptr_t commit_index = msg[3];
    // see if leader is same leader 
    update_state(term, leader);
	
    if ((replica.id != leader)) {

        set_tag(&msg[0], RAFT_APPR);
	    // term < currentTerm
	    if ((term < replica.current_term)) {
            // failed !
            mp_send7(replica.replicas[leader],
                     msg[0],
                     replica.current_term,
                     prev_index,
                     replica.id,
                     false,
                     0, 0);
	        return;
	    }
	
	    // log doesn't contain entry at prev_log_index whose term matches prev_log_term
        //print_queue_state(&replica.queue);
	    if (!queue_contains(&replica.queue, prev_index)) {
            mp_send7(replica.replicas[leader],
                     msg[0],
                     replica.current_term,
                     prev_index,
                     replica.id,
                     false,
                     0,0);
	        return;
	    }   


        if (queue_contains(&replica.queue, prev_index+1)) {
	    // conflict -> delete existing entry and all that follow it
	        uint64_t entry_index = prev_index+1;
	        while (remove_entry(&replica.queue, entry_index) != NULL) {
		           entry_index++;
	        }
	    }

	    // append log
        struct log_entry* ele = (struct log_entry*) malloc(sizeof(struct log_entry));
        ele->index = prev_index+1;
        ele->term = term;
        ele->header = msg[0];
        ele->payload[0] = msg[4];        
        ele->payload[1] = msg[5];        
        ele->payload[2] = msg[6];        
        replica.last_log_index = prev_index+1;

        enqueue(&replica.queue, ele);
	    if (commit_index > replica.commit_index) {
	        replica.commit_index = MIN(commit_index, replica.last_log_index);
	    }
        mp_send7(replica.replicas[replica.current_leader],
                 msg[0],
                 replica.current_term,  
                 prev_index+1,
                 replica.id,
                 true,
                 0, 0);
    } else { // forward to leader
	    printf("Replica %d: Leader should not receive appendEntry requests \n", replica.id);
    }

    update_applied_entries();
}


static void handle_vote(uintptr_t* msg){

}

static void handle_vote_response(uintptr_t* msg){

}
/*
static void handle_requestVote_request(struct raft_binding *b,
		       uint64_t cand_term,
		       uint16_t cand_id,
		       uint64_t last_log_index,
		       uint64_t last_log_term)
{
#ifdef DEBUG_LEADER_FAIL
    printf("Replica %d: received requestVote_request !! \n", replica.id);
#endif 
    replica.election_timeout = false;
    struct raft_binding *candidate = replica.replica_bindings[cand_id];

    if (replica.voted_for == cand_id) {
       send_requestVote_reply(candidate, replica.current_term, true);
       return;
    }

    // term is smaller -> do not grant vote
    if (replica.current_term > cand_term) {
#ifdef DEBUG_LEADER_FAIL
	    printf("Replica %d: requestVote_request current_term > cand_term -> fail\n", replica.id);
#endif 
        send_requestVote_reply(candidate, replica.current_term, false);
        return;
    }

    // log is at least up to date and did not vote yet
    if (replica.voted_for == -1){
        // if the term is larger then the log is more up to date -
        // ->grant vote
        if (last_log_term > replica.term[last_log_index]) {  
            replica.voted_for = cand_id;
            send_requestVote_reply(candidate, replica.current_term, true);
#ifdef DEBUG_LEADER_FAIL
	        printf("Replica %d: requestVote_request last_log_term > term -> success\n", replica.id);
#endif 
	        return;
        } else if ((last_log_term == replica.current_term) &&
		  (last_log_index >= replica.last_log_index)) {
            // if the terms are equal but the last log entry is at least as
	        // large as mine -> grant vote i.e. more or equal up to date
            replica.voted_for = cand_id;
            send_requestVote_reply(candidate, replica.current_term, true);
#ifdef DEBUG_LEADER_FAIL
	        printf("Replica %d: requestVote_request last_log_index >= replica.last_log_index -> success\n", 
		        replica.id);
#endif 
	        return;
        } else {
	      // log is less up to date -> do not grant vote
#ifdef DEBUG_LEADER_FAIL
    	  printf("Replica %d: requestVote_request log not up to date-> fail\n", 
		        replica.id);
#endif 
          send_requestVote_reply(candidate, replica.current_term, false);
        }


        return;
    } else { // voted already
#ifdef DEBUG_LEADER_FAIL
	    printf("Replica %d: requestVote_request voted already-> fail\n", 
		replica.id);
#endif 
        send_requestVote_reply(candidate, replica.current_term, false);
    }
}
*/
/*
static void handle_requestVote_reply(struct raft_binding *b,
		       uint64_t cand_term,
		       bool granted)
{
    errval_t err;
    // already leader (for votes that are more than the majority)
    if (replica.current_leader == replica.id) {
	    return;
    }

    if (granted) {
    	replica.num_votes++;
    } else {
	    replica.num_rejects++;		
    }

    // requestVote only started if one replica failed -> replica.num_replica-1
    if (replica.num_votes > (replica.num_replicas-1)/2) {
        // become leader
        replica.is_leader = true;
        replica.is_candidate = false;
        replica.is_follower = false;
        replica.current_leader = replica.id;
        replica.num_votes = 0;
        replica.voted_for = -1;
        printf("Replica %d: I'm the new leader !! \n", replica.id);
#ifdef DEBUG_LEADER_FAIL
        printf("Replica %d: last_log_index %"PRIu64" \n",replica.id, replica.last_log_index);
        printf("Replica %d: commit_index %"PRIu64" \n",replica.id, replica.commit_index);
        printf("Replica %d: last_applied %"PRIu64" \n",replica.id, replica.last_applied);

        for (int i = 0; i < replica.num_requests; i++) {
            printf("Replica %d: log[%d] %"PRIu64" term[%d] %"PRIu64" \n", 
                    replica.id, i, replica.log[i], i, replica.term[i]);
        }
#endif 
        // everything that is not applied will be resent by the client
        for (int i = 0; i < replica.num_replicas;i++) {
	        replica.next_index[i] = replica.last_log_index+1;
	        replica.match_index[i] = replica.last_applied;
        }

        update_commit_index_leader();

        // send empty append entry
#ifdef USE_TREE_MCAST
	    multicast_tree(-1, true, true);
#else
	    multicast(-1, true, true);
#endif
	    multicast_to_clients();

	    // deffered event for heartbeat
	    err = periodic_event_create(&heartbeat_timer, &tree_ws, HEARTBEAT_TIMEOUT*1000,
				       MKCLOSURE(heartbeat_cb, NULL));
	    if (err_is_fail(err)) {
	        printf("Replica %d: creating periodic event failed \n", replica.id);
	    }
	
	    err = periodic_event_cancel(&election_timer);
	    if (err_is_fail(err)) {
	        printf("Replica %d: destroying periodic event failed \n", replica.id);
	    }
    }
    
    // vote failed for me -> return to follower state
    if (replica.num_rejects >= replica.num_replicas/2) {
	replica.voted_for = -1;
	replica.num_votes = 0;
	replica.num_rejects = 0;
	replica.is_candidate = false;
	replica.is_follower = true;

	err = periodic_event_cancel(&election_timer);
	if (err_is_fail(err)) {
	   printf("Replica %d: destroying periodic event failed \n", replica.id);
	}

	// since I was rejected from majority set timer to highest 
	err = periodic_event_create(&election_timer, &tree_ws, (ELECTION_TIMEOUT+BACKOFF_MAX)*1000,
					MKCLOSURE(election_cb, NULL));
	if (err_is_fail(err)) {
	   printf("Replica %d: destroying periodic event failed \n", replica.id);
	}
	// TODO:try again later ?
    }
}
*/

static void handle_append_response(uintptr_t* msg)
{
    //printf("Repica %d: handle append response \n", replica.id);
    uint32_t term = (uint32_t) msg[1];
    uint64_t last_index = msg[2];
    uint8_t replica_id = (uint8_t) msg[3];
    bool success = (bool) msg[4];
    struct log_entry* ele = NULL;

    update_state(term, replica.current_leader);

    
    if (success) { // send next
    //    printf("term %d last_index %" PRIu64 " replica_id %d success %d \n",
    //          term, last_index, replica_id, success);
	    replica.next_index[replica_id] = last_index+1;
	    replica.match_index[replica_id] = last_index;
        ele = queue_contains(&replica.queue, last_index);
        ele->exec_count++;

        update_commit_index_leader();
	    // if there are some outstanding entries for this replica
	    // send them
        
	    if (replica.last_log_index >= replica.next_index[replica_id]) {
            printf("Resending to %d \n", replica_id);
            printf("Log index %" PRIu64 " next_index %" PRIu64 "\n", 
                    replica.last_log_index,
                    replica.next_index[replica_id]);
            set_tag(&msg[0], RAFT_APP);
            set_request_id(&msg[1], replica.current_term);
            set_tag(&msg[1], replica.id);

            ele = ele->next;
            assert(ele != NULL);
            mp_send7(replica.replicas[replica_id],
                    msg[0], msg[1], last_index,
                    replica.commit_index,
                    ele->payload[0],
                    ele->payload[1],
                    ele->payload[2]);
	    }  
    } else { // send previous
    //    printf("term %d last_index %" PRIu64 " replica_id %d success %d \n",
    //          term, last_index, replica_id, success);
	    // check if we already match the entry the replcia request 
        if (last_index < (replica.match_index[replica_id])) {
	        return;
	    }

	    replica.next_index[replica_id] = last_index-1;

        assert(ele != NULL);
        ele = ele->prev;

        mp_send7(replica.replicas[replica_id],
                 msg[0], msg[1], last_index-1,
                 replica.commit_index,
                 ele->payload[0],
                 ele->payload[1],
                 ele->payload[2]);

    }

    update_applied_entries();
}


/*
static void election_cb(void* arg) {

	// Assume tie i.e. no leader elected -> reset vote
	if (replica.is_candidate) {
	   replica.voted_for = -1;
	   replica.num_votes = 0;
	   replica.num_rejects = 0;
	}

	// Assume last vote failed reset
	if ((replica.is_follower) && !(replica.voted_for == replica.current_leader)) {
	   replica.voted_for = -1;
	}

	if (replica.election_timeout && ((replica.voted_for == -1) || replica.is_candidate)){
#ifdef DEBUG_LEADER_FAIL
            printf("Replica %d: Trying to become leader (already cand %d) \n", replica.id, 
			      replica.is_candidate);
#endif
	     replica.current_term++;

	     replica.is_follower = false;
	     replica.is_candidate = true;
	     replica.is_leader = false;
	     replica.num_votes = 1;
	     replica.num_rejects = 0;
	     replica.voted_for = replica.id;
#ifdef USE_MCAST_TREE
	     multicast_tree(-1, false, false);
#else	  
	     multicast(-1, false, false);
#endif	
	}
    replica.election_timeout = true;  
}

static void heartbeat_cb(void* arg)
{
     if (replica.id == replica.current_leader) {
	// somtimes got stuck -> udpate periodically the 
	// commit index and the applied entries
        update_commit_index_leader();
	    update_applied_entries(replica.id);
	// multicast heartbeat
#ifdef USE_MCAST_TREE
        multicast_tree(-1, false, false);
#else	  
        multicast(-1, true, true);
#endif		
     }
}
*/
void set_execution_fn_raft(void (*exec_fn)(void *))
{
     replica.exec_fn = exec_fn;
}

static void default_exec_fn(void* addr);
static void default_exec_fn(void* addr)
{
	return;
}

void init_replica_raft(uint8_t id,
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
                       void (*exec_fn)(void*))
{
	replica.id = id;
	replica.current_term = 1;
	// just set 0 as the leader at the beginning
	replica.current_leader = 0;
	replica.commit_index = 0;
	replica.last_applied = 0;
	replica.num_clients = num_clients;
	replica.num_replicas = num_replicas;
	replica.num_requests = num_requests;
    replica.clients = clients;
    replica.replicas = replicas;
    replica.node_size = node_size;
    replica.level = level;
    replica.alg_below = alg_below;
    replica.started_from = started_from;
    replica.current_core = current_core;

	replica.last_log_index = 0;
	replica.num_votes = 0;
	replica.voted_for = -1;
	replica.backoff = (rdtsc() % BACKOFF_MAX);

	for (int i = 0; i < num_replicas; i++) {
	    replica.next_index[i] = 2;
	    replica.match_index[i] = 0;
	}

    log_queue_init(&replica.queue);
    struct log_entry* ele = (struct log_entry*)
                            malloc(sizeof(struct log_entry));

    ele->index = 0;
    ele->term = 1;
    enqueue(&replica.queue, ele);

    if (exec_fn == NULL) {
        replica.exec_fn = default_exec_fn;
    } else {
        replica.exec_fn = exec_fn;
    }

    init_stats(&replica.avg);
    replica.runs = 0;

	if (id == replica.current_leader) {
	   replica.is_leader = true;
	   replica.is_follower = false;
	   replica.is_candidate = false;
	} else {
	   replica.is_leader = false;
	   replica.is_follower = true;
	   replica.is_candidate = false;
	}

#ifndef LIBSYNC
    if (replica.id == 0) {
        for (int i = 1; i < replica.num_replicas; i++) {
            mp_connect(current_core, replicas[i]);
        }
    }
#endif

#ifdef MEASURE_TP
    if ((id == 0) && (level == NODE_LEVEL)) {
       pthread_t tid;
       pthread_create(&tid, NULL, results_raft, &replica);
    }
#endif
}
