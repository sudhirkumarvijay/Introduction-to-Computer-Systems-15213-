/* ----------------------------------------------------------------------------
 * File: cache.c
 * Name: Sudhir Kumar Vijay
 * Andrew ID: svijay@andrew.cmu.edu
 * Private dependencies - csapp.c csapp.h cache.h
 * ----------------------------------------------------------------------------
 * Cache is implemented using a Least-Recently-Used (LRU) policy, where-in a 
 * linked-list based queue scheme is implemented. The least-recently used node, 
 * consisting of the oldest cached buffer is evicted, when there is a need to
 * make room for newer buffer entries. To maintain size of the cache, the size 
 * of the input buffer is first checked to be below MAX_OBJECT_SIZE and then
 * if the total size, after addition is lesser than MAX_CACHE_SIZE. If the 
 * expected size is more than the MAX_CACHE_SIZE then the LRU blocks are 
 * evicted to make room for the new entries .
 * ----------------------------------------------------------------------------
*/

#include <stdio.h>
#include "csapp.h"
#include "cache.h"
#include <string.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Defining and initializing static global variables */ 
static cache_element* head = NULL;
static cache_element* tail = NULL;
static unsigned current_cache_size = 0; /* Used to keep track of cache size */

/* Debug define */
/* Uncomment to enable print messages */
// #define DEBUG_VERBOSE

/* ----------------------------------------------------------------------------
 * Function: cache_init 
 * Input parameters: query, buf_val, size of entry to be initialized 
 * Return parameters: -None- 
 * ----------------------------------------------------------------------------
 * Description: 
 * Initializes the cache with the first entry.
 * ----------------------------------------------------------------------------
 */
void cache_init(char* query, char* buf_val, size_t size){
    #ifdef DEBUG_VERBOSE
    printf("cache init: query:%s \n", query);
    #endif
    cache_element* node;
    node = Malloc(sizeof(cache_element));
    node->size = size;
    node->cache_query = strdup(query);
    node->cache_buf = malloc(size);
    memcpy(node->cache_buf, buf_val,size);
    node->next=NULL;
    node->prev=NULL;
    head = node;
    tail = head;
}

/* ----------------------------------------------------------------------------
 * Function: add_to_queue 
 * Input parameters: query, buf_val, size of entry to be added 
 * Return parameters: -None- 
 * ----------------------------------------------------------------------------
 * Description: 
 * Mallocs and adds a node to the cache queue.
 * ----------------------------------------------------------------------------
 */
void add_to_queue(char* query, char* buf_val, size_t size){
    #ifdef DEBUG_VERBOSE
    printf("Add_to_queue: query:%s \n", query);
    #endif
    cache_element* node;

    if(head == NULL){
    /* If cache is empty */
        cache_init(query, buf_val, size);        
    } else {
    /* Add to start of queue */
        node = Malloc(sizeof(cache_element));
        node->size = size;
        node->cache_query = strdup(query);
	node->cache_buf = malloc(size);
	memcpy(node->cache_buf, buf_val,size);
        node->next = head;
        node->prev = NULL;

        head->prev = node;
        head = node;
    }
    current_cache_size += size; 
}

/* ----------------------------------------------------------------------------
 * Function: find_node 
 * Input parameters: Cache_query to be found.
 * Return parameters: Pointer to node containing the query. Query here is the
 * URI key corresponding to the cache entry.
 * ----------------------------------------------------------------------------
 * Description: 
 * Finds and returns the linked list element containing the buffer entry
 * ----------------------------------------------------------------------------
 */
cache_element* find_node(char *query){
    cache_element* rover;
    for(rover = head; rover!=NULL; rover = rover->next){
        if(strcmp(query,rover->cache_query)==0){
            #ifdef DEBUG_VERBOSE
            printf("find_node:Input Query found = %s \n",query);
            printf("find_node:Query found = %s \n", rover->cache_query);
            #endif
            return rover;
        }
    }
    return NULL;
}

/* ----------------------------------------------------------------------------
 * Function: delete_from_cache 
 * Input parameters: node to be deleted 
 * Return parameters:  -None- 
 * ----------------------------------------------------------------------------
 * Description: 
 * Deletes a corresponding node from the cache. 
 * Most importantly - frees all the Malloced resources to prevent memory leaks. 
 * ----------------------------------------------------------------------------
 */
void delete_from_cache(cache_element* del_node){
    #ifdef DEBUG_VERBOSE
    printf("delete_from_cache - query: %s\n", del_node->cache_query);
    #endif
    
    if(del_node == NULL){
        #ifdef DEBUG_VERBOSE
        printf("No node to delete exists !\n");
        #endif
        return;
    }

    /* If node is the head */
    if(del_node == head){
        if(del_node->next ==NULL){
            /*If only element in cache*/
            head = NULL;
            tail = NULL;
        } else {
            /* Deleting head */
            del_node->next->prev = NULL;
            head = del_node->next;
        }
    } else if(del_node->next == NULL){
        /* If node is the tail, deleting tail*/
        del_node->prev->next = NULL;
        tail = del_node->prev;
    } else {
        /* Deleting interior node*/
        del_node->next->prev = del_node->prev; 
        del_node->prev->next = del_node->next;
    }
    /* Decreasing size of cache */
    current_cache_size -= del_node->size;
    /* Freeing and resetting node resources */
    del_node->prev = NULL;
    del_node->next = NULL;
    del_node->size = 0;
    free(del_node->cache_query);
    free(del_node->cache_buf);
    free(del_node);
}


/* ----------------------------------------------------------------------------
 * Function: add_to_cache
 * Input parameters: data to be added to cache (query, buf_value,size of 
 * buffer) 
 * Return parameters: -None- 
 * ----------------------------------------------------------------------------
 * Description: 
 * Adds a new cache element, if-and-only if the size of the new buffer entry is 
 * less than MAX_OBJECT_SIZE and new size of cache is less than MAX_CACHE_SIZE.
 * If new size of cache exceeds MAX_CACHE_SIZE, then delete LRU elements to 
 * accomodate the new, validly sized buffer.
 *
 * To be called only if no current element already exists.
 * ----------------------------------------------------------------------------
 */
void add_to_cache (char* query, char* buf_val, size_t size){
    #ifdef DEBUG_VERBOSE
    printf("Add_to_cache - query: %s|size= %u\n", query, (unsigned int)size);
    #endif
    unsigned new_size = current_cache_size + size;
    
    /* Return if size of object exceeds maximum*/
    if(size > MAX_OBJECT_SIZE){
        #ifdef DEBUG_VERBOSE
        printf("Maximmum size exceeded!\n");
        #endif
        return ;
    }
    
    /* Add to cache if new size cahce is less than MAX_CACHE_SIZE,
     * else delete LRU objects to accomodate new entry */ 
    if (new_size <= MAX_CACHE_SIZE) {
        add_to_queue(query, buf_val, size);
    } else {
        while(new_size > MAX_CACHE_SIZE){
            delete_from_cache(tail);
            new_size = current_cache_size + size;
        }
        add_to_queue(query, buf_val, size);
    }
}

/* --------- DEBUG FUNCTIONS ---------------- */
/* ----------------------------------------------------------------------------
 * Function: print_cache 
 * Input parameters: -None- 
 * Return parameters: -None- 
 * ----------------------------------------------------------------------------
 * Description: 
 * Debug helper function to print entire cache.
 * ----------------------------------------------------------------------------
 */
void print_cache(void)
{
    #ifdef DEBUG_VERBOSE
    cache_element* rover;
    printf("*** Printing Cache **** \n");
    for(rover = head; rover!=NULL; rover = rover->next){
        print_element(rover);
        printf("-- \n");
    }
    printf("*** End of Cache **** \n");
    #endif
}

/* ----------------------------------------------------------------------------
 * Function: print_element 
 * Input parameters: Node 
 * Return parameters: -None- 
 * ----------------------------------------------------------------------------
 * Description: 
 * Debug helper function to print cache node.
 * ----------------------------------------------------------------------------
 */
void print_element(cache_element* node)
{
    #ifdef DEBUG_VERBOSE
    printf("size = %u\n", (unsigned)node->size);
    printf("query = %s\n", node->cache_query);
    printf("next = %p\n", node->next);    
    printf("prev = %p\n", node->prev);   
    #endif 
}

