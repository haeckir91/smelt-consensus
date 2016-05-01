#include "quorum.h"
#include "barrelfish_compat.h"
#include "ump_conf.h"
#include "lxaffnuma.h"
#include "model_defs.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>

extern coreid_t num_cores;
extern char* my_name;

extern __thread coreid_t this_core;
__thread struct node **nodes;

struct process *processes;

pthread_barrier_t barrier_init_done;

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"

// bidirectional (i.e., with two channels) UMP pair
// fwr: forward src->dst
// rev: reverse dst->src
struct ump_bi_pair_state {
    struct ump_pair_state *fwr, *rev;
};

#define UMP_CONF_INIT(src_, dst_, shm_size_)      \
{                                                 \
    .src = {                                      \
         .core_id = src_,                         \
         .shm_size = shm_size_,                   \
         .shm_numa_node = numa_cpu_to_node(src_)  \
    },                                            \
    .dst = {                                      \
         .core_id = dst_,                         \
         .shm_size = shm_size_,                   \
         .shm_numa_node = numa_cpu_to_node(src_)  \
    },                                            \
    .shared_numa_node = -1,                       \
    .nonblock = 1                                 \
}

/*
 * \brief Initialize a UMP connection
 *
 * 
 */
void init_ump_pair(coreid_t src, coreid_t dst, 
                   struct node **ret_send, struct node **ret_receive)
{
    struct ump_bi_pair_state st;

    struct ump_pair_conf fwr_conf = UMP_CONF_INIT(src, dst, UMP_BUFFER_SIZE);
    struct ump_pair_conf rev_conf = UMP_CONF_INIT(dst, src, UMP_BUFFER_SIZE);

    st.fwr = ump_pair_state_create(&fwr_conf);
    st.rev = ump_pair_state_create(&rev_conf);

    struct node *send_node = malloc(sizeof(struct node));
    *send_node = (struct node) { 
        .node_idx = dst,
        .send = &st.fwr->src,
        .recv = &st.rev->dst,
        .next = NULL
    };

    struct node *receive_node = malloc(sizeof(struct node));
    *receive_node = (struct node) { 
        .node_idx = src,
        .send = &st.rev->src,
        .recv = &st.fwr->dst,
        .next = NULL
    };

    
    if (ret_send!=NULL)    *ret_send = send_node;
    if (ret_receive!=NULL) *ret_receive = receive_node;
}

static void register_ump_pair(coreid_t src, coreid_t dst, 
                              struct node *send_node,
                              struct node *receive_node)
{
    // Storing channels in node datastructures.
    // This is thread-safe since every node is only writing its 

    // Enqueue child
    struct node* prev = NULL;
    struct node* c = processes[src].children;

    // Walk linked list
    while (c!=NULL) {
        prev = c;
        c = c->next;
    }

    if (prev!=NULL) {
        // Enqueue
        prev->next = send_node;
    } else {
        processes[src].children = send_node;
    }

    // Set parent
    QDBG("register_ump_pair(%d,%d): Setting parent for %d\n", src, dst, dst);
    assert(processes[dst].parent == NULL);
    processes[dst].parent = receive_node;

    // Remember this child
    nodes[send_node->node_idx] = send_node;
    
    printf("%d -> %d\n", src, dst);
}

/*
 * \brief Find children and connect to them
 *
 */
static void tree_bind_to_children(void)
{
    /*
     * The model indicates the order in which MP channels should be
     * served. Lower integer values in the model array indicate that a
     * message on that channel should be served first. 
     *
     * Note: IDs >0 and <SHM_SLAVE_START indicate MP channels.
     *
     * The atomic broadcast implementation serves channels in the
     * order given by the linked list of children. Hence, we need to
     * init channel in the order indicated by the model.
     */
    for (int weight=1; weight<SHM_SLAVE_START; weight++) {

        for (coreid_t node=0; node<num_cores; node++) {
        
            // Establish a UMP channel in the direction of flow in the tree
            // Also: Establish LAST_NODE to SEQUENTIALIZER 
            if (model_is_parent(this_core, node) && 
                model_get_mp_order(this_core, node)==weight) {

                QDBG("tree_bind_to_children: adding from model (%d,%d)\n", 
                     node, this_core);

                struct node *send;
                struct node *receive;
                init_ump_pair(this_core, node, &send, &receive);
                register_ump_pair(this_core, node, send, receive);
            }
        }
    }

}

static void tree_sanity_check(void)
{
    // SANITY CHECK
    // Make sure all bindings as suggested by the model are set up
    for (int i=0; i<num_cores; i++) {

        if (!model_is_in_group(i))
            continue;
        
        // All processes have a parent
        if (processes[i].parent == NULL) {
            printf("Node %d does not have a parent\n", i);
        }
        assert(processes[i].parent != NULL || i==SEQUENTIALIZER);

        int numc = 0;
        struct node *n = processes[i].children;
        while (n!=NULL) {
            n = n->next;
            numc++;
        }

        QDBG("node %d has %d children\n", i, numc);
    }
}

/*
 * \brief Initialize a broadcast tree from a model.
 *
 */
int tree_init(void)
{
    // Sequentializer initializes some data structures
    if (this_core==SEQUENTIALIZER) {

        processes = malloc(sizeof(struct process)*num_cores);
        assert(processes!=NULL);
        memset(processes, 0, sizeof(processes));

    }

    nodes = (struct node**) malloc(num_cores*sizeof(struct node*));

    QDBG("building tree .. \n");
    for (int round=0; round<num_cores; round++) {

        if (this_core==round) {
            QDBG("tree_init: round %d on %d: connecting to other clients .. \n",
                 round, this_core);
            tree_bind_to_children();
        }
    }

    pthread_barrier_wait(&barrier_init_done);
    QDBG("Init complete on node %d\n", this_core);

    if (processes[this_core].parent!=NULL) {

        nodes[processes[this_core].parent->node_idx] = 
            processes[this_core].parent;
    }

    if (this_core == SEQUENTIALIZER) {
        tree_sanity_check();
    }

    return 0;
}
