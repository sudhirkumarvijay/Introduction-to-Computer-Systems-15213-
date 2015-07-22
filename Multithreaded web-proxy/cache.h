/* Sudhir Kumar Vijay
 * svijay@andrew.cmu.edu
 * ----------------------------------------------------------------------------
 * Header file for cache.c
 * ----------------------------------------------------------------------------
 */

#ifndef __CACHE_H__
#define __CACHE_H__
#include "csapp.h"

typedef struct cache_element{
	size_t size;
	char* cache_query;
	char* cache_buf;
	struct cache_element* next;
	struct cache_element* prev;
} cache_element;

void cache_init(char* query, char* buf_val, size_t size);
void add_to_queue(char* query, char* buf_val, size_t size);
cache_element* find_node(char*query);
void delete_from_cache(cache_element* del_node);
void add_to_cache (char* query, char* buf_va, size_t size);

void print_cache(void);
void print_element(cache_element* node);
#endif
