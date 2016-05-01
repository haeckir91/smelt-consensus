#ifndef __CLIST_H
#define __CLIST_H

#include <stdbool.h>

/**
 * Code based on:
 * https://github.com/chriscamacho/clist
 */

/**
 * structure representing a node in a list
 */
typedef struct __cnode cnode_t;

/**
 * Struct representing state required to keep track of 
 * a COMMIT REQUEST along the subtree starting at current node.
 */
struct state2pc {

    coreid_t core;
    uint32_t version;
    uint32_t num_resp;

    bool accept;
};

struct state2pc *init_state(coreid_t core, uint32_t version)
{
    struct state2pc *ret = malloc(sizeof(struct state2pc));

    ret->core = core;
    ret->version = version;
    ret->num_resp = 0;

    ret->accept = true;

    return ret;
}

struct __cnode {
    /*@{*/
    struct __cnode *prev;/**< pointer to the previous node in the list */
    struct __cnode *next;/**< pointer to the next node in the list */
    struct state2pc *data;/**< pointer to the data this node is tracking */
    /*@}*/
};


/**
 * structure representing a list of nodes (basically just a header)
 */
typedef struct __clist clist_t;

struct __clist {
    /*@{*/
    cnode_t *head;/**< the first node in the list */
    cnode_t *tail; /**< the last node in the list */
    /*@}*/
};

/** @brief
 * creates a list
 *
 * @details
 * Allocates space for the list structure clearing the
 * head and tail pointer ready for use.
 *
 * @return provides a pointer to the newly created list
 */
clist_t *clistCreateList() {
    clist_t *new=malloc(sizeof(clist_t));
    new->head=NULL;
    new->tail=NULL;
    return new;
}

/** @brief
 * adds a node to a list
 *
 * @details
 * allocates space for a new node and places the node at
 * the end of the list
 *
 * @param [in] list the list to add the node to
 * @param [in] data the data that this node points to
 *
 * @return the newly allocated node
 */
cnode_t *clistAddNode(clist_t *list, struct state2pc *data) {
    cnode_t *new=malloc(sizeof(cnode_t));
    new->prev=list->tail;
    new->next=NULL;
    if (new->prev!=NULL) new->prev->next=new;
    if (list->head==NULL) list->head=new;
    list->tail=new;
    new->data=data;
    return new;
}

/** @brief
 * search for a node in the list
 *
 * @details
 * brute force search for a node pointing to specific data
 * generally you should be iterating lists and not dealing
 * with specific nodes individually but this could come
 * in handy
 *
 * @param [in] list the list you want to find the node in
 * @param [in] ptr finds the node that points to this data
 *
 * @return checks each node in turn looking for a node that
 * points to ptr returns NULL if it doesn't find a match
 */
cnode_t *clistFindNode(clist_t *list, coreid_t core, uint32_t version) {
    cnode_t *node=list->head;
    while (node!=NULL) {
        if (node->data->core==core && node->data->version==version) return node;
        node=node->next;
    }
    node=NULL;
    return node;
}

/** @brief
 * delete a node
 *
 * @details this unlinks the node from the list and then frees its
 * memory (the user is responsible for the data the nodes data pointer points to)
 * the pointer to the node (pnode) is then NULLed to aid bug and resource
 * checking
 *
 *
 * @param [in] list the list containing the node you wish to delete
 * @param [in] pnode a pointer to the node pointer you want to delete
 *
 * @param [out] pnode the node pointer is returned NULLed
 */
void clistDeleteNode(clist_t *list,cnode_t **pnode) {
    cnode_t *node=*pnode;
    if (node->prev!=NULL) node->prev->next=node->next;
    if (node->next!=NULL) node->next->prev=node->prev;
    if (list->head==node) list->head=node->next;
    if (list->tail==node) list->tail=node->prev;
    free(*pnode);
    *pnode=0;
}

#endif
