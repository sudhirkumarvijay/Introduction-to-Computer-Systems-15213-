/******************************************************
 * Name - Sudhir Kumar Vijay
 * Andrew ID - svijay 
 * 
 * DESCRIPTION:
 * This program implements a simple cache simulator. 
 * Given a value of the number of bits used to represent 
 * a set (s), the number of lines (E) and the number of 
 * bits used to represent the block offset (b), the simulator
 * gives the number of hits, misses and evictions for a 
 * particular trace case.
 ********************************************************/

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include "cachelab.h"

/* Defining and initializing global variables
 * set_bits: Number of bits for sets. 
 * block_bits: Number of bits for block offset.
 * number_of_lines: Number if cache lines in each set.
 * number_of_sets: Total number of cache blocks (S*E).
 * LRU_count: Counter used to find the LRU block in a set.
*/
int hit_count = 0;   
int miss_count = 0;
int eviction_count = 0;

int set_bits = 0;
int block_bits = 0; 

int number_of_lines = 0; 
int number_of_sets = 0; 
int LRU_count;

/* Definining the structure cache block - the individual cell 
 * inside the cache aray. 
 * valid - This bit is set to '0' intially and then '1'
 *             to simulate cold misses
 * tag    - Tag bit of the current line.
 * block_LRU_count - Used to indicate last valid access.
 *                      The lowest block_LRU_count in each line is
 *                      evicted in case of a miss.
 * */
struct cache_block {
    int valid;
    int tag;
    int block_LRU_count;
};

/* Function - cache_access
 * Defining and implementing a cache access function, 
 * which calculates the number of hits, misses and 
 * evictions based on the current address location 
 * of the cache acccess 
 * ---------------------------------------------------
 * Input parameters: 
 * Pointer to cache and current address of trace.
 * --------------------------------------------------
 * Return value:
 * Modified pointer to cache after modifications.
 * --------------------------------------------------
 * */
struct cache_block* cache_access(struct cache_block* cache, int address){
    int block_LRU_count_smallest = 0;
    int evict_block_suspect_index = 0;
    int found_block = 0;    
    int j = 0;
    int set_offset = 0;
    
    // Extracting the set number and tag_number from address.
    int set_address_mask = (~(-1 << (set_bits+block_bits)));
    int set_number = (address & set_address_mask) >> block_bits; 
    int tag_number = (address >> (set_bits+block_bits)) & set_address_mask;

    // Finding the offset value to traverse the cache array.    
    set_offset = number_of_lines*set_number;
    
    // Pointers to traverse the cache array.
    struct cache_block *current_block;
    struct cache_block *current_set;
    struct cache_block *evict_block_suspect;

    // Current row in cache.
    current_set = cache + set_offset;     

    // Update LRU_count to indicate cache request.
    // The lowest LRU_count value in each set will be 
    // evicted in case of a miss.
    LRU_count++; 

    // Search for corresponding tag across all lines in the current set.
    for (j = 0; j < number_of_lines; j++) {
        current_block = current_set + j;
        // If the tag is found, then we've found the right block
        if ((current_block->tag) == tag_number) {   
            if ((current_block->valid) == 0) {
                // In case of cold miss, setting valid bit to '1'.
                // Also incrementing miss_count by 1.
                (current_block->valid) = 1;
                (current_block->block_LRU_count) = LRU_count;
                miss_count++;
            } else { 
                // In case of hit, incrementing hit count.
                (current_block->block_LRU_count) = LRU_count;
                hit_count++;
            }
            found_block = 1;
            break;
        } else {
            found_block = 0;
        }
    }    

    // If no corresponding tag is found, then evict the LRU block out 
    // of the current set. This should also take care of the cold misses
    // missed in the code above.
    if (found_block == 0) {
        miss_count++;
 
        // Finding the LRU block's index
        block_LRU_count_smallest = (current_set->block_LRU_count);
        for (j = 0; j< number_of_lines; j++){
            current_block = current_set + j;
            if (current_block->block_LRU_count < block_LRU_count_smallest){
                // Update smallest block_LRU_count.
                block_LRU_count_smallest = (current_block->block_LRU_count); 
                // Update index of current evict block suspect.
                evict_block_suspect_index = j;                    
            } 
        }

        // Evicting LRU block
        evict_block_suspect = current_set + evict_block_suspect_index;
        evict_block_suspect->tag = tag_number;
        evict_block_suspect->block_LRU_count = LRU_count;

        if (evict_block_suspect->valid == 1) {
            // If valid block, then increment eviction count.
            // Evict block suspect confirmed.
            eviction_count++;
        } else {
            // Instead, if cold miss block encountered, set valid bit.
            evict_block_suspect->valid = 1;
        }
    }
    return cache;    
}


/* Main function
 * ---------------------------------------------------
 * Input parameters: 
 * argc and argv from stdin.
 * --------------------------------------------------
 * Return value:
 * Returns an integer(0) on exit. 
 * --------------------------------------------------*/
int main(int argc, char *argv[]) {
    // Defining necessary variables.
    int opt;
    int i = 0;
    int j = 0;

    unsigned address = 0;
    unsigned numbytes = 0;

    char *trace_file_name;
    FILE *trace_file;
    char action;
    struct cache_block *cache;

    // Cache datastructures    
    while ((opt = getopt(argc, argv, "s:E:b:t:")) != -1) {
        switch(opt) {
            case 's':
                set_bits = atoi(optarg);
                break;
            case 'E':
                number_of_lines = atoi(optarg);
                break;
            case 'b':
                block_bits = atoi(optarg);
                break;
            case 't':
                trace_file_name = optarg;
                break;
            default:
                // Assumption: User wants to exit incase wrong 
                // format is found.
                printf("Invalid input format. Correct format is: \n");
                printf("./csim -s <s> -E <E> -b <b> -t <tracefile>\n");
                exit(1);
        }
    }

    // Opening tracefile for reading data.
    trace_file  = fopen(trace_file_name, "r");
    // Assumption: User wishes to exit incase tracefile is not found    
    if (trace_file == NULL){
        printf("No valid tracefile found \n");
        exit(2);
    }
    
    // Allocate space for cache block.
    // Ignoring the block offset as it was mentioned.
    number_of_sets =  (1<<set_bits)*number_of_lines;    
    cache = malloc(number_of_sets*sizeof(struct cache_block));    
    if (cache == NULL){
        // Exiting in case no free space available for malloc or any
        // other error.
        printf("Malloc error !");
        exit(3);
    }

    // Initialize cache block, setting valid bits to
    // zero to set it up for initial cold misses. Also
    // initializing the other variables to zero.
    for (i = 0; i < (1<<set_bits); i++) {
        for (j = 0; j < number_of_lines; j++) {
            (cache+(i+j))->valid = 0;
            (cache+(i+j))->tag = 0;
            (cache+(i+j))->block_LRU_count = 0;
        }
    }
    
    // Scanning each tracefile for loads, stores and modifies.
    while (fscanf(trace_file," %c %x,%d", &action, &address, &numbytes)>0) {
        switch(action) {
            case 'L':
                // Load case
                cache_access(cache, address);
                break;
            case 'S':
                // Store case
                cache_access(cache, address);
                break;
            case 'M':
                // Accessing the cache twice since move is a load
                // followed by a store
                cache_access(cache, address);
                cache_access(cache, address);
                break;
            default:
                // In case rogue space/character(Eg.'I') is encountered 
                // in the trace file, just continue to next line.
                continue;
        }
    }
    fclose(trace_file);
    free(cache);
    printSummary(hit_count, miss_count, eviction_count);
    return 0;
}
