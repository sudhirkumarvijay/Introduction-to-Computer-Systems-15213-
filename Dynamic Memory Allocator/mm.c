/* -----------------------------------------------------------------------------
 * File: mm.c
 * Name: Sudhir Kumar Vijay
 * Andrew ID: svijay@andrew.cmu.edu
 * -----------------------------------------------------------------------------
 * A dynamic memory allocation implemented using segregated free lists. 
 * The data-structure used to store payload data consists of a 4-byte header.
 * Once a block is freed, it is added to a free list segregated according to 
 * differently sized bins. After freeing, the payload's first two bytes are 
 * overwritten with 4-byte offsets to the previous and sucessor blocks in the
 * corresponding segregated list. 
 * 
 * The data-structures for allocated and free blocks are desribed in the 
 * diagrams below:
 * 
 * ALLOCATED BLOCK SCHEMATIC:
 *    ----------------------------------------
 *    | SIZE   | PREV_ALLOC |  CURRENT_ALLOC |  <-- HEADER BLOCK (4-bytes)
 *    ----------------------------------------
 *    |             PAYLOAD                  |  <-- PAYLOAD BLOCK
 *    ----------------------------------------
 * Note that there is no footer for an allocated block. This design 
 * decision was chosen after taking into advantage of the fact that the
 * size of the block that is indicated in the header always has its last three
 * bits set to zero (since all the blocks are double word aligned). This design
 * effectively reduces the utilzation by reducing the required overheard and 
 * hence prevents unnecessary fragmentation. A particular case where the adjust-
 * ed size of the block is increased by 8 bytes, over the original size, just 
 * to accomodate the footer overhead was noticed frequently. This design 
 * prevents the extra overhead in such a scenario (the adjusted block size is
 * calculated inside the mm_malloc function).
 *
 * FREE BLOCK SCHEMATIC:
 *    ----------------------------------------
 *    | SIZE   | PREV_ALLOC |  CURRENT_ALLOC | <-- HEADER BLOCK (4-bytes)
 *    ----------------------------------------
 *    |             PRED                     | <-- POINTER OFFSET TO PREDESSOR
 *    ----------------------------------------     BLOCK (4-bytes)
 *    |             SUCC                     | <-- POINTER OFFSET TO SUCCESSOR
 *    ----------------------------------------     BLOCK (4-bytes)
 *    |        ORIGINAL PAYLOAD              | <-- ORIGINAL PAYLOAD CLOBBERED
 *    |           (CLOBBERED)                |     DUE TO INSERTION OF POINTERS
 *    |                                      |     AND FOOTER.
 *    ----------------------------------------     
 *    |          SIZE        | CURRENT_ALLOC | <-- FOOTER BLOCK (4-bytes)
 *    ----------------------------------------
 * 
 * The free blocks have space for a footer (this unfortunately places a minimum
 * restriction on the adjusted size of the block calculated in the 'mm_malloc'
 * function and also in the minimum size of a split block, calculated in the
 * the 'place' function.
 * -----------------------------------------------------------------------------
 * HEADER DESCRIPTION: 
 * The header consists of three distinct parts - the size of the current block,
 * the allocation status bit of the preceding block in the heap and the 
 * allocation status of the current block:
 * ~ Size - Contains the total size (including overhead) of the current block.
 * Since the blocks are double word aligned, the last three bits of the size 
 * are always zero. This fact is taken as an advantage by using the LSB and the 
 * second last significant bit to denote allocation status.
 * ~ Prev_alloc - Contain allocation status of the previous block. It is set to 
 * 1 in case the previous block is allocated and 0 in case it's  free. This 
 * provides the advantage of the not needing a footer to indicate the 
 * allocation status of the preceding block and provides higher utilization by 
 * reducing fragmentation in the allocation blocks.
 * ~ Current_alloc - Denotes the allocation status of the current block. Set to 
 * 0 if the current block is free and 1 if the current block is allocated.
 * -----------------------------------------------------------------------------
 * FOOTER DESCRIPTION:
 * An allocated block does not have a footer as described above.Every free 
 * block consists consists of a footer that is used while coalescing. The 
 * consists of the following parts:
 * ~ Size - Contains the total size (including overhead) of the current block.
 * Size in header and footer should be the same.
 * ~ Current_alloc - Denotes the allocation status of the current block. Set to 
 * 0 if the current block is free and 1 if the current block is allocated.
 * -----------------------------------------------------------------------------
 * SEGREGATED LIST POLICY:
 * A segregated list is used to contains all the free blocks in the current
 * state. The list is divided into 'bins' of different sizes with a total
 * number of bins set by the #define BIN_SIZE (set to 7). 
 * 
 * ~ Addition into the list - A block that needs to be added to a list is first
 * checked for its size and hence its destination bin. Then, the block is 
 * added at the beginning of the corresponding linked-list of free blocks. The
 * newly added block is then set as the head.
 * ~ Deletion from the list - The bin of the block to be deleted is first dete-
 * rmined. Then, the block is deleted from the list, making sure that the 
 * blocks preceding and succeeding it are pointed to point towards each other, 
 * and thus maintining the consistency of the linked list. This has been tested
 * extensively using the mm_checkheap() function.
 * ~ Splitting a block on the list - This is carried out by the 'block-split' 
 * function during a 'place' call. During a malloc call with a small size, it
 * could be the case wherein the block that has been found in the free list or
 * one that has been obtained by newly extending the heap may have a much 
 * larger size. To reduce fragmentation, the free block needs to be split and
 * the request size chunk needs to be returned to be used by the user program.
 * If the now-smaller free block is in the same segregated bin as the 
 * previously unsplit block, then it needs to take the place of its parent. 
 * This is accomplished by copying the predecessor and successor pointers
 * to the newly split block and also re-assigning the predecessor and successor
 * blocks to point to the newly split block. In the case that the newly split
 * block is smaller that threshold size (16-bytes), it is simply deleted from 
 * the list.
 * 
 * To determine the size of the buckets, several tests were run using tracefiles
 * The maximum utilization, with maximum throughput was found using the
 * following bin policy:
 * BIN_SIZE = 7
 * BIN - 0:  blocksize <= 50
 * BIN - 1:  50   < blocksize <= 100
 * BIN - 2:  100  < blocksize <= 1000,
 * BIN - 3:  1000 < blocksize <= 2000,
 * BIN - 4:  2000 < blocksize <= 3000,
 * BIN - 5:  3000 < blocksize <= 4500,
 * BIN - 6:  blocksize > 4500
 * -----------------------------------------------------------------------------
 * POINTER POLICY:
 * In each of the free blocks, 4 byte pointer offsets are used, instead of the
 * full 8-byte pointer value. This optimization was possible due to the 2^32
 * limitation of a dynamic memory allocation query.
 * The offset is calculated by subtracting the heap_listp value from the actual
 * pointer value. This helps in improving utlization.
 * -----------------------------------------------------------------------------
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mm.h"
#include "memlib.h"

/* Used to enable the debug option inside the functions */
/* Uncomment to enable */
/* #define DEBUG */

/* Used to include verbosity in checkheap, prints all statements if enabled */
/* Uncomment to enable */
/* #define VERBOSE_CHECKHEAP */

#ifdef DRIVER
/* Create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

/* Basic constants and macros */
#define WSIZE       4      /* Word and header/footer size (bytes) */ 
#define DSIZE       8      /* Doubleword size (bytes) */
#define CHUNKSIZE  (1<<6)  /* Extend heap by this amount (bytes) */ 

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) 
#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            
#define PUT(p, val)  (*(unsigned int *)(p) = (val))    

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_PREV_ALLOC(p) (GET(p) & 0x2)

/* Set/deset prev_alloc bit in the next block */
#define HDRP_N_BLK(p) HDRP(NEXT_BLKP(p))
#define SET_NEXT_ALLOC(p)   PUT(HDRP_N_BLK(p), (GET(HDRP_N_BLK(p)) | 0x2));
#define SET_NEXT_DEALLOC(p) PUT(HDRP_N_BLK(p), (GET(HDRP_N_BLK(p)) & ~(0x2)));

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                     
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Calculating successor and predecessor pointer offsets */
#define PRED(bp) (unsigned*)bp
#define SUCC(bp) (unsigned*)((char*)(bp)+WSIZE)

/* Given pointer to successor location, calculate the predessor pointer */
#define PREDFROMSUCC(sp) (unsigned*)((char*)(sp)-WSIZE)

/* Calculating the pointer value of successor and predecessor block by adding
 * the heap_listp_val (HEAP_NULL) to the pointer offsets */
#define HEAP_LISTP_VAL  ((unsigned long) heap_listp)
#define GET_P(bp)       *(unsigned *)(bp)  
#define GETPREDVAL(bp)  (unsigned long)GET_P(PRED(bp))
#define GETSUCCVAL(bp)  (unsigned long)GET_P(SUCC(bp))
#define PREDPOINT(bp)   (unsigned *)(GETPREDVAL(bp)+HEAP_LISTP_VAL)  
#define SUCCPOINT(bp)   (unsigned *)(GETSUCCVAL(bp)+HEAP_LISTP_VAL) 

/* Filling a pointer location with a value */
#define PUT_P(bp,val)  *(unsigned *)(bp) = ((unsigned )val) 

/* Given a block pointer, calculating the pointer to the next block in list*/
#define NEXTP(bp) PREDFROMSUCC(SUCCPOINT(bp))

/* Defining the heap_listp_val as a 'HEAP_NULL' to denote start/end of list */
#define HEAP_NULL (unsigned *) (HEAP_LISTP_VAL)

/* Given a pointer 'p', calculating the offset value */
#define P_OFFSET_VAL(p) (unsigned) ((unsigned long) p - HEAP_LISTP_VAL) 

/* Calculating the current size of the block */
#define CURR_SIZE(bp) GET_SIZE(HDRP(bp))

/* Defining the number of size bins in the segregated bin */
#define BIN_SIZE 7

/* Global variables */
static char *heap_listp = 0;  /* Pointer to first block */  

/* Global pointer to the start of the segregated list. This is an array of 
 * segregated list heads, each pointing to the start of a segregated bin in 
 * the list
 */
unsigned **seglist_head = NULL ;

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void check_heap_block(void *bp);
static void check_free_block(void *bp);
static void add_to_list(void* bp);
static void delete_from_list(void* bp);
static void block_split (void* bp, int index);
static int  get_seg_index(size_t blocksize);
static void check_cycle (unsigned* head);

#ifdef VERBOSE_CHECKHEAP
static void print_free_block(void *bp); 
static void print_heap_block(void *bp) ;

/* Helper function to print free block without checkheap*/
static inline void PRINT_FREE_BLOCK(void* bp) {
    printf("* Printing free block *\n"); 
    printf("* At line %d in function %s* \n", __LINE__, __func__); 
    print_free_block(bp);
}

/* Helper function to print free block without checkheap*/
static inline void PRINT_HEAP_BLOCK(void* bp) {
    printf("* Printing heap block *\n"); 
    printf("* At line %d in function %s* \n", __LINE__, __func__); 
    print_heap_block(bp);
}
#endif

/* ----------------------------------------------------------------------------
 * Function: mm_init
 * Input parameters: -none-
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description:
 * mm_init initializes the heap everytime a new trace iteration is carried out.
 * It is responsible for extending the heap and updating the global variables -
 * 'heap_listp'and 'seglist_head'. 'heap_listp' is the pointer to the footer of 
 * prologue block and 'seglist_head' denotes the pointer to the location of the
 * segmented list heads.
 * 
 * The function returns -1 in the case that 'extend_heap' is not succussful in
 * allotting a new heap and 0 on success.
 * ----------------------------------------------------------------------------
 */
int mm_init(void) {
    /* Creating the initial empty heap */
    if ((heap_listp = mem_sbrk(BIN_SIZE*DSIZE + 4*WSIZE)) == (void *)-1)
        return -1;

    /* Setting the segregated list head to point to the start of the heap */
    seglist_head = (unsigned **) heap_listp;
    /* Initializing the segregated list heads */
    memset(seglist_head, 0, BIN_SIZE*DSIZE);
    
    heap_listp += (BIN_SIZE*DSIZE);              
    PUT(heap_listp, 0);                          /* Alignment padding */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */ 
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */ 
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     /* Epilogue header */

    heap_listp += (2*WSIZE);                
    SET_NEXT_ALLOC(heap_listp)
    
    /* Extend the empty heap with a free block of chunksize bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) 
        return -1;
    return 0;
}

/* ----------------------------------------------------------------------------
 * Function: malloc
 * Input parameters: size of block to be allocated.
 * Return parameters: Pointer to allocated block.
 * ----------------------------------------------------------------------------
 * Description:
 * 'malloc' returns the pointer to an allocated block. In case the heap is not
 * initialized (i.e. if the 'heap_listp' pointer is null), then 'malloc' calls
 * mm_init which sets up the heap. In case the size of the block requested 
 * is 0, 'malloc' returns NULL.
 * The size is rounded of to the nearest double word (8 bytes) in the case that
 * the requested size is greater than a double word, or else (in the case that 
 * the requested size is less than a double word) it is set to 16 bytes.
 * This is done so that the appropriate overhead blocks (header, predecessor 
 * and successor pointers) can be added to the blocks which have been freed.
 * Once the size of the block is found out, 'malloc' then searches the entire 
 * free list for a block and if found, malloc returns the pointer of the free 
 * block after placement (by calling the 'place' function). If no block is 
 * found, then 'malloc' calls the 'extend_heap' function to extend the heap.
 * If 'extend_heap' fails to allot a new heap, then NULL is returned.
 * ----------------------------------------------------------------------------
 */
void *malloc (size_t size) {
    size_t asize;      /* Defining adjusted block size */
    size_t extendsize; /* Defining amount to extend heap if no fit */
    char *bp;     

    /* Initializing heap_listp if not done */
    if (heap_listp == 0){
        mm_init();
    }

    /* Ignoring spurious requests */
    if (size == 0)
        return NULL;

    /* Calculating the adjested size */
    if (size <= 1*DSIZE)                                          
        asize = (2*DSIZE);                            
    else
        asize = DSIZE * ((size + (DSIZE) + (WSIZE-1)) / DSIZE); 

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {  
        place(bp, asize);     
        #ifdef DEBUG            
            mm_checkheap(__LINE__);
        #endif
        return bp;
    }

    extendsize = MAX(asize,CHUNKSIZE);               
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;         
    place(bp, asize);
    #ifdef DEBUG            
            mm_checkheap(__LINE__);
    #endif   
    return bp;
}


/* ----------------------------------------------------------------------------
 * Function: free
 * Input parameters: Pointer to block.
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description:
 * 'free' frees an allocated block of memory (using the pointer to that block).
 * In case the pointer is null then free returns without doing anything, also
 * in case the heap isn't initialized during the 'free' call, then mm_init() is 
 * called to initialize the heap with a free chunk of memory.
 * The block is first assigned to a free status by setting the allocation lsb 
 * bit-0 to zero and preserving the allocation bit of the previous block 
 * (lsb bit-1). The 'coalesce' function is then called to determine the status
 * of the blocks before and after the currently freed block and coalesce if 
 * necessary.
 * ----------------------------------------------------------------------------
 */
void free (void *bp) {
    if(bp == 0) 
        return;

    size_t size = GET_SIZE(HDRP(bp));
    /* If no heap, call mm_init and create one */
    if (heap_listp == 0){
        mm_init();
    }

    /* Preserving the previous_alloc bits */
    PUT(HDRP(bp), PACK(size, GET_PREV_ALLOC(HDRP(bp))));
    PUT(FTRP(bp), PACK(size, 0));
    SET_NEXT_DEALLOC(bp);    
    coalesce(bp);
}

/* ----------------------------------------------------------------------------
 * Function: realloc
 * Input parameters: Pointer to allocated block and size to be reallocated.
 * Return parameters: Pointer to reallocated block.
 * ----------------------------------------------------------------------------
 * Description:
 * The 'realloc' function is used to change the size of an allocated block, in 
 * case a valid size and pointer to an existing allocated block is provided. In 
 * the case that the reallocated size is zero, then a free function is called,
 * since that's the equivalent of a zero size block. Similarly, in the case 
 * that a null pointer is provided, the 'realloc' function calls the 'malloc' 
 * function to allocate a block of input size. 
 * If the realloc fails, then no change is made to the original block and a 
 * NULL pointer is returned.
 * ----------------------------------------------------------------------------
 */
 void *realloc(void *ptr, size_t size) {
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        mm_free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(ptr == NULL) {
        return mm_malloc(size);
    }

    newptr = mm_malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data into new block */
    oldsize = GET_SIZE(HDRP(ptr));
    if(size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    mm_free(ptr);
    return newptr;
}

/* ----------------------------------------------------------------------------
 * Function: calloc
 * Input parameters: Number of blocks and size of each block
 * Return parameters: Pointer to allocated blocks.
 * ----------------------------------------------------------------------------
 * Description:
 * The 'calloc' function is used to allocate and initilize the values in a 
 * block to zero. This is accomplished by calculating the size of the memory to
 * be allocated. Then, the 'malloc' function is called with this size and the 
 * memory of the blocks whose pointer is returned by the 'malloc' call is set
 * to zero using the 'memset' function. The pointer to the newly intialized 
 * block is returned.
 * ----------------------------------------------------------------------------
 */
void *calloc (size_t num_blocks, size_t size) {
  size_t total_bytes = num_blocks * size;
  void *ptr;

  /* Calling a 'malloc' with cumulative size of memory */
  ptr = malloc(total_bytes);

  /* Setting 'total_bytes' bytes of memory pointed by 'ptr' to zero */
  memset(ptr, 0, total_bytes);

  return ptr;
}

/* --- HELPER FUNCTIONS --- */

/* ----------------------------------------------------------------------------
 * Function: coalesce
 * Input parameters: Block pointer to coalesce
 * Return parameters: Pointer to coalesced blocks
 * ----------------------------------------------------------------------------
 * Description: 
 * 'coalesce' combines blocks and returns the pointer to the newly coalesced 
 * blocks. In the case that no coalescing is required, it returns the input
 * pointer. 
 *
 * Coalescing is carried out by detecting the allocated condition of the next 
 * and previous blocks using the last two LSB bits:
 * ~ If the last LSB bit (bit-0) is set then it indicates that the current 
 * block is allocated, or else it is free.
 * ~ If the second last LBS bit (bit-1) is set then it indicates that the 
 * previous block is allocated, or it else is free to be coalesced.
 * 
 * Coalescing by this manner provides the advantage that the footer for alloca-
 * ted blocks can be fully removed and the footer for the free blocks indicates
 * the size for coalescing. The allocation status of the previous and next 
 * blocks are denoted using the 'prev_alloc' and 'next_alloc' variables.
 * 
 * The cases for coalescing are described below:
 * Case 1: Previous and next blocks are allocated.
 * The block is added to the free list at the head without coalescing.
 * Case 2: Previous block is allocated and next block is not allocated.
 * The next block and the current block are colaesced and the new block with the
 * merged size is added to the start of the corresponding segregated free list.
 * Case 3: Previous block is free and next block is allocated. 
 * The current and previous blocks are merged and the new block is added to the
 * appropriate segregated free list.
 * Case 4: Both previous and next blocks are free and hence all three blocks 
 * are combined and the new block is added to the start of the free list.
 * ----------------------------------------------------------------------------
 */
static void *coalesce(void *bp) 
{
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size =       GET_SIZE (HDRP(bp));

    if (prev_alloc && next_alloc) {            
        /* Case 1: Both blocks are allocated */
        /* Adding current block to free list and updating prev_alloc bit of
         * next block */
        add_to_list(bp);
        SET_NEXT_DEALLOC(bp);
        return bp;
    }
    else if (prev_alloc && !next_alloc) {      
        /* Case 2 : Next block is free */
        /* Deleting next block to free list and updating prev_alloc bit of
         * block after next block */
        delete_from_list(PRED(NEXT_BLKP(bp)));
        SET_NEXT_ALLOC(PRED(NEXT_BLKP(bp)));

        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        /* Assigning free allocation condition for current block */
        PUT(HDRP(bp), PACK(size, GET_PREV_ALLOC(HDRP(bp))));
        PUT(FTRP(bp), PACK(size,0));
        
        /* Adding cumulative block to free list and updating prev_alloc bit of
         * next block */
        add_to_list(PRED(bp));
        SET_NEXT_DEALLOC(PRED(bp));
    }
    else if (!prev_alloc && next_alloc) {      
        /* Case 3 : Previous block is free */
        /* Deleting previous block from free list and updating prev_alloc bit of
         * current block */
        delete_from_list(PRED(PREV_BLKP(bp)));
        SET_NEXT_ALLOC(PRED(PREV_BLKP(bp)));

        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        /* Assigning free allocation condition for previous and current block */
        PUT(FTRP(bp), PACK(size, 0));
        prev_alloc = GET_PREV_ALLOC(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, prev_alloc));

        /* Adding cumulative block to free list and updating prev_alloc bit of
         * next block */
        bp = PREV_BLKP(bp);
        add_to_list(PRED(bp));   
        SET_NEXT_DEALLOC(PRED(bp));
    }
    else {                                     
        /* Case 4 : Both previous and next blocks are free */
         /* Deleting previous and next block from free list and updating 
         * prev_alloc bit of current block */       
        delete_from_list(PRED(PREV_BLKP(bp)));
        delete_from_list(PRED(NEXT_BLKP(bp)));
        SET_NEXT_ALLOC(PRED(PREV_BLKP(bp)));
        SET_NEXT_ALLOC(PRED(NEXT_BLKP(bp)));

        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
        GET_SIZE(FTRP(NEXT_BLKP(bp)));
        prev_alloc = GET_PREV_ALLOC(HDRP(PREV_BLKP(bp)));

        /* Adding cumulative block to free list and updating prev_alloc bit of
         * next block */
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, prev_alloc));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        add_to_list(PRED(bp));    
        SET_NEXT_DEALLOC(PRED(bp));
    }
    #ifdef DEBUG            
            mm_checkheap(__LINE__);
    #endif   
    return bp;
}

/* ----------------------------------------------------------------------------
 * Function: get_seg_index
 * Input parameters: Block size of the current block
 * Return parameters: Segregated list index of corresponding bin.
 * ----------------------------------------------------------------------------
 * Description: 
 * This is used to find the index of the segregated list bin using the size
 * of the block. 
 * For example, 
 * bin 0: Sizes 0-10
 * bin 1: Sizes 11-20
 * bin 2: Sizes 21-50 and so on.
 * In this case, get_seg_index(25) will return 2.
 * ----------------------------------------------------------------------------
 */
static int get_seg_index(size_t blocksize) {
    int seg_index = 0 ;
    if(blocksize <= 50){
        seg_index = 0;
    } else if ((blocksize>50) && (blocksize <= 100)) {
        seg_index = 1;
    } else if ((blocksize>100) && (blocksize <= 1000)) {
        seg_index = 2;        
    } else if ((blocksize>1000) && (blocksize <= 2000)) {
        seg_index = 3;
    } else if ((blocksize>2000) && (blocksize <= 3000)) {
        seg_index = 4;
    } else if ((blocksize>3000) && (blocksize <= 4500)) {
        seg_index = 5;
    } else if (blocksize > 4500) {
        seg_index = 6;
    }
    return seg_index;
}

/* ----------------------------------------------------------------------------
 * Function: add_to_list
 * Input parameters: Block pointer
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description: 
 * This function is used to add the block denoted by the block-pointer to the 
 * start of the corresponding segregated free list and also updating the head
 * of the changed list. In case the segregated free list was empty, it adds the
 * new block to the list and sets it as the head of the single-element free 
 * list.
 * ----------------------------------------------------------------------------
 */
static void add_to_list(void* bp){
    size_t blocksize = GET_SIZE(HDRP(bp));
    int head_index = get_seg_index(blocksize);
    if (seglist_head[head_index] == NULL){
        /* If adding first block to list */
        seglist_head[head_index] = bp; /* Updating head of the seglist */ 
        PUT_P(PRED(bp), 0);
        PUT_P(SUCC(bp), 0);
    }
    else {
        /* Adding a block to existing list */
        PUT_P (PRED (seglist_head[head_index]), P_OFFSET_VAL(PRED(bp)));
        /* Updating successor value with pointer offset of current head */
        PUT_P (SUCC (bp), P_OFFSET_VAL(SUCC(seglist_head[head_index])));
        PUT_P (PRED (bp), 0);
        seglist_head[head_index] = bp; /* Updating head of the seglist */ 
    }
}

/* ----------------------------------------------------------------------------
 * Function: delete_from_list
 * Input parameters: Block pointer
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description: 
 * This function deletes the block denoted by the block pointer. Care is taken 
 * while calling, such that a NULL block is given a delete call, so in the case
 * where a delete of a NULL block is called, the program exits with an error.
 * Deleting a block involves the following cases:
 *
 * Case 1: Block is the head
 * If the list has a single element, then the list head is updated to NULL
 * If the list has other elements, then the next element in the list is set as
 * the head.
 * 
 * Case 2: Block is the tail
 * If the block to be deleted is the tail in the list, then the second last 
 * element's successor pointer is set to HEAP_NULL (and thus, the tail is 
 * updated).
 * 
 * Case 3: Block is in the middle
 * If the block is in the middle of the list, then the pointers inside the next 
 * and previous blocks are changed to point to each other, thereby 'bypassing'
 * the current block.
 * 
 * Note that in all cases, the predecessor and successor pointers in the 
 * current block are set to NULL to effectively delete it from the list.
 * ----------------------------------------------------------------------------
 */
static void delete_from_list(void* bp) {
    size_t blocksize = GET_SIZE(HDRP(bp));
    /* Finding the index in the segregated list */
    int head_index   = get_seg_index(blocksize);
    if (bp == seglist_head[head_index]) {
        /* if bp is the head of the list */
        if(SUCCPOINT(bp)==HEAP_NULL){
            /* If bp is the only element in the free list */
            /* Updating head of the seglist to NULL */ 
            seglist_head[head_index] = NULL; 
        } else {
            /* If other elements exist, assign the next guy as the head */
            seglist_head[head_index] = NEXTP(bp);
            PUT_P(PRED(seglist_head[head_index]), 0);
        }
    }  
    else if(SUCCPOINT(bp)==HEAP_NULL) {
        /* if bp is the tail of the list */
        PUT_P(SUCC(PREDPOINT(bp)), 0);
    } 
    else {
        /* Pointing values of previous and next blocks */
        PUT_P(PREDFROMSUCC(SUCCPOINT(bp)), GET_P(PRED(bp)));
        PUT_P(SUCC(PREDPOINT(bp)), GET_P(SUCC(bp)));
    }
    /* Assign NULL to predecessor and successor offsets */
    PUT_P(PRED(bp), 0);       
    PUT_P(SUCC(bp), 0) ;   
    return ;
}

/* ----------------------------------------------------------------------------
 * Function: block_split
 * Input parameters: Block pointer
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description: 
 * This function performs an in-place delete of the placed block, wherein
 * it updates the newly split-out free block with the successor and predecessor
 * pointers of the old, previously larger and unsplit, free block.
 * 
 * Case 1: Old block was the head
 * The newly split block is assigned as head. If the list had other elements, 
 * then the newly split block is pointed to the next element.
 * 
 * Case 2: Old block is the tail
 * If the old block to be deleted is the tail in the list, then the second last 
 * element's successor pointer is set to point to the newly split block. 
 * 
 * Case 3: Block is in the middle
 * If the block is in the middle of the list, then the pointers inside the next 
 * and previous blocks are changed to point to the newly split block.
 * 
 * Note that in all cases, the predecessor and successor pointers in the old, 
 * bigger block are copied to the newly split block.
 * ----------------------------------------------------------------------------
 */
static void block_split (void* bp, int index){
    int head_index = index;
    void* bp_next;
    bp_next = NEXT_BLKP(bp);

    /* Copying values of predecessor & sucessor offsets into newly split block*/
    PUT_P(PRED(bp_next), GET_P(PRED(bp)));
    PUT_P(SUCC(bp_next), GET_P(SUCC(bp)));

    if (bp == seglist_head[head_index]){
        /* Both blocks belong to the same seg list */     
        if(SUCCPOINT(bp)==HEAP_NULL){
            /* If bp is the only element in the free list */
            seglist_head[head_index] = PRED(bp_next);
        } else {
            /* If other elements exist after bp */
            PUT_P(PREDFROMSUCC(SUCCPOINT(bp)), P_OFFSET_VAL(PRED(bp_next)));
            seglist_head[head_index] = PRED(bp_next);
        }
    }  
    else if(SUCCPOINT(bp)==HEAP_NULL) {
        /* if bp is the tail of the list */
        PUT_P(SUCC(PREDPOINT(bp)), P_OFFSET_VAL(SUCC(bp_next)));
    } 
    else {
        /* Pointing values of previous and next blocks to newly split block*/
        PUT_P(PREDFROMSUCC(SUCCPOINT(bp)), P_OFFSET_VAL(PRED(bp_next)));
        PUT_P(SUCC(PREDPOINT(bp)), P_OFFSET_VAL(SUCC(bp_next)));
    }
    /* Removing connections to allocated block by pointer offsets to 0*/
    PUT_P(PRED(bp), 0) ;
    PUT_P(SUCC(bp), 0) ;
    return;
}

/* ----------------------------------------------------------------------------
 * Function: extend_heap
 * Input parameters: Number of  words to extend
 * Return parameters: Pointer to the newly extended_heap block.
 * ----------------------------------------------------------------------------
 * Description: 
 * This function performs a heap extension by the closest even-number rounded
 * number of words to the input arguement. It sets the header and footer of the
 * newly allocated chunk to free and also preserves the previous-block 
 * allocation bit (i.e. the 2nd last bit from the LSB). In addition, it sets 
 * the previous-block allocation bit of the next block to zero, to imply that
 * the current block is free. Coalesce is called on the newly allocated heap
 * block.
 * ----------------------------------------------------------------------------
 */
static void *extend_heap(size_t words) 
{   
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; 
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;  

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, GET_PREV_ALLOC(HDRP(bp))));/* Header */   
    PUT(FTRP(bp), PACK(size, 0));         /* Footer */   
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */ 

    SET_NEXT_DEALLOC(bp)   ; /*Updating status of next block's previous_alloc*/

    /* Coalesce if the previous block was free */
    return coalesce(bp);                                          
}

/* ----------------------------------------------------------------------------
 * Function: place
 * Input parameters: Block pointer of the block to be placed and size of new
 *                   allocated block.
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description: 
 * This function detects whether or not a particular  block that is newly 
 * needs to be split into a smaller free block that replaces it in the free 
 * list.
 * If the block is large enough to be split (i.e. the new free block that is 
 * created is larger than (2*DSIZE)), then the block is added to the free-list
 * either in the same location of the previously large block, or in a different
 * bin(placement carried out by the block_split function).
 * If the block is smaller than this threshold, then it is simply deleted from 
 * the segregated free list.
 * In addition, the previous_block_allocation bits are appropriately set in the
 * block proceeding the current block.
 * ----------------------------------------------------------------------------
 */
static void place(void *bp, size_t asize)
{
    void *bp_old;
    size_t csize = GET_SIZE(HDRP(bp));   
    int old_index;

    if ((csize - asize) >= (2*DSIZE)) { 
        if(get_seg_index(csize-asize) == get_seg_index(csize)){
            old_index = get_seg_index(csize);
            /*New block is in same seglist as old one */
            /*Performing an in-place swap */
            PUT(HDRP(bp), PACK(asize, GET_PREV_ALLOC(HDRP(bp)) | 0x1));
            SET_NEXT_ALLOC(bp);                 
            bp_old = bp;
            bp = NEXT_BLKP(bp);
            PUT(HDRP(bp), PACK(csize-asize, GET_PREV_ALLOC(HDRP(bp))));
            PUT(FTRP(bp), PACK(csize-asize, 0));
            SET_NEXT_DEALLOC(bp);                     
            block_split(bp_old, old_index);
        } else {
            /* New block is in a different seg list from the old from */
            /* Deleting the old one and adding the new one to its seg list */
            delete_from_list(bp);
            PUT(HDRP(bp), PACK(asize, GET_PREV_ALLOC(HDRP(bp)) | 0x1));
            SET_NEXT_ALLOC(bp);           
            bp = NEXT_BLKP(bp);
            PUT(HDRP(bp), PACK(csize-asize, GET_PREV_ALLOC(HDRP(bp))));
            PUT(FTRP(bp), PACK(csize-asize, 0));
            add_to_list(bp);
            SET_NEXT_DEALLOC(bp);           
        }
    }
    else { 
        SET_NEXT_ALLOC(bp);           
        PUT(HDRP(bp), PACK(csize, GET_PREV_ALLOC(HDRP(bp)) | 0x1));
        delete_from_list(PRED(bp));  
    }
}

/* ----------------------------------------------------------------------------
 * Function: find_fit
 * Input parameters: Size of the block to be found.
 * Return parameters: Pointer to block that meets size requirements.
 * ----------------------------------------------------------------------------
 * Description: 
 * A first-fit search is employed across the free list to find a a possible
 * block that matches the minimum size requirments. 
 * First, the appropriate bin in the segregated free list is determined, by
 * calling the 'get_seg_index' function. Then, the members of the corresponding
 * segregated bin are searched for a size match. If none are found, then the
 * search resumes in the next higher sized bin (i.e index + 1). If none are
 * found throughtout the free list search, then a NULL is returned.
 * ----------------------------------------------------------------------------
 */
static void *find_fit(size_t asize)
{
    /* First fit search */
    unsigned *bp = NULL;
    unsigned *current_head = NULL;
    int head_index;
    int index;
    head_index = get_seg_index(asize);
    for(index = head_index; index<=(BIN_SIZE-1); index++){
        current_head = seglist_head[index];
        if(current_head != NULL) {
            for (bp = current_head; SUCCPOINT(bp)!=HEAP_NULL; bp = NEXTP(bp)){
                if (asize <= GET_SIZE(HDRP(bp))) {
                    return bp;
                }
            }
            /* Comparing for the last block, not covered in loop */
            if (asize <= GET_SIZE(HDRP(bp))) {
                return bp;
            }
        }
    }
    return NULL; /* No fit */
}

/* DEBUG FUNCTIONS */

/* ----------------------------------------------------------------------------
 * Function: in_heap
 * Input parameters: Pointer to block.
 * Return parameters: 1 if inside heap, and 0 if outside.
 * ----------------------------------------------------------------------------
 * Description: 
 * Function that returns 1 if the current pointer is within the heap boundaries
 * Else returns zero.
 * ----------------------------------------------------------------------------
 */
static int in_heap(const void *p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/* ----------------------------------------------------------------------------
 * Function: check_heap_block
 * Input parameters: Pointer to current heap block.
 * Return parameters: 1 if check heap conditions are satisfied, 0 if not.
 * ----------------------------------------------------------------------------
 * Description: 
 * The function checks for the following conditions and exits if even one of 
 * them is not met. It checks them for all the blocks in the heap.
 * Conditions checked for: 
 * 1) Double word alignment
 * 2) Allocation status of current block matches the previous_block_allocation 
 * status bit of the next block. 
 * Please note that there is no footer in case of an allocated block and hence
 * the sizes are not checked (the are checked in the 'check_free_block').
 * __func___ just displays check_heap_block (intentional - for easy debug)
 * ----------------------------------------------------------------------------
 */
static void check_heap_block(void *bp) {
    /*Checking alignment */
    if ((size_t)bp % 8) {
        printf("check_heap_block Error: %p is not doubleword aligned\n", bp);
        exit(-1);
    }
    /*Checking current/next allotment status*/
    if (GET_ALLOC(HDRP(bp)) != (!!GET_PREV_ALLOC(HDRP(NEXT_BLKP(bp))))) {
        printf("%s Size Error: Alloc status doesn't match \n", __func__);
        exit(-1);
    }
    /* Checking if no two free blocks exist in succession */
    if ((GET_ALLOC(HDRP(bp))==0) && (GET_ALLOC(HDRP(NEXT_BLKP(bp)))==0)) {
        printf("%s Size Error: Coalesce failed, two free blocks \n", __func__);
        exit(-1);
    }
    /* Checking for if pointer is in heap */
    if(in_heap(bp) != 1){
        printf("%s Error: Pointer not in heap \n",  __func__);
        exit(-1);
    }   
}

/* ----------------------------------------------------------------------------
 * Function: check_free_block
 * Input parameters: Pointer to the current free block.
 * Return parameters: 1 if check free block conditions are satisfied, 0 if not.
 * ----------------------------------------------------------------------------
 * Description: 
 * The function checks for the following conditions and exits if even one of 
 * them is not met. 
 * Conditions checked for: 
 * 1) Double word alignment
 * 2) Allocation status of header and footer should match
 * 3) Size of header and footer should match.
 * 4, 5) Pointers to the next and previous blocks are connected properly
 * the current block - i.e. connectivity of linked list.
 * __func___ just displays check_free_block (intentional - for easy debug)
 * ----------------------------------------------------------------------------
 */
static void check_free_block(void *bp) {
    /* Checking for alignment */
    if ((size_t)bp % 8) {
        printf("%s Error: %p is not doubleword aligned\n", __func__, bp);
        exit(-1);
    }

    /* Checking for allocation status of header and footer */
    if ((GET_ALLOC(HDRP(bp))) != (GET_ALLOC(FTRP(bp)))) {
        printf("%s Alloc Error: header doesn't match footer  \n", __func__);
        exit(-1);
    }

    /* Checking for sizes in of header and footer */
    if ((GET_SIZE(HDRP(bp))) != (GET_SIZE(FTRP(bp)))) {
        printf("%s Size Error: header doesn't match footer \n", __func__);
        exit(-1);
    }

    /* Checking if predecessor and successor pointers are consistent */
    if(SUCCPOINT(bp) != HEAP_NULL){
        if (PRED(bp) != PREDPOINT(PREDFROMSUCC(SUCCPOINT(bp)))) {
            printf("%s Error: Successor block is disconnected \n",  __func__);
            exit(-1);
        }
    }
    if(PREDPOINT(bp) != HEAP_NULL){
         if (SUCC(bp) != SUCCPOINT(PREDPOINT(bp))) {
            printf("%s Error: Predecessor block is disconnected \n",  __func__);
            exit(-1);
        }
    }

    /* Checking for if pointer is in heap */
    if(in_heap(bp) != 1){
            printf("%s Error: Pointer not in heap \n",  __func__);
            exit(-1);
    }   
}

#ifdef VERBOSE_CHECKHEAP
/* ----------------------------------------------------------------------------
 * Function: print_heap_block
 * Input parameters: Pointer to the current heap block.
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description: 
 * The function is invoked only during a verbose checkheap call. 
 * Prints out the contents of the current heap, in a specific format.
 * Please note that the footer contents of an allocated block may not match
 * its header.
 * ----------------------------------------------------------------------------
 */
static void print_heap_block(void *bp) {
    int hsize, halloc, palloc, fsize, falloc;

    /* Size in header, current and previous block allocation status bits */
    hsize  = (int) GET_SIZE(HDRP(bp));
    halloc = (int) GET_ALLOC(HDRP(bp));  /*Status of current_alloc bit*/
    palloc = (int) GET_PREV_ALLOC(HDRP(bp));  /*Status of previous_alloc bit*/
    
    /* Size in footer, current status bits */
    fsize  = (int) GET_SIZE(FTRP(bp));
    falloc = (int) GET_ALLOC(FTRP(bp));  

    if (hsize == 0) {
        printf("%p: EOL\n", bp);
        return;
    }

    printf("%p: header: [%d:%c:%c] footer: [%d:%c]\n", bp,
    hsize, (palloc ? 'a' : 'f'), (halloc ? 'a' : 'f'), 
    fsize, (falloc ? 'a' : 'f')); 
}

/* ----------------------------------------------------------------------------
 * Function: print_free_block
 * Input parameters: Pointer to the current free block.
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description: 
 * The function is invoked only during a verbose checkheap call. 
 * Prints out the contents of the current free block along with their locations
 * in memory in a specific format.
 * ----------------------------------------------------------------------------
 */
static void print_free_block(void *bp) {
    if (bp != NULL){
        int get_alloc = GET_PREV_ALLOC(HDRP(bp)) || GET_ALLOC(HDRP(bp));
        printf(" -------------------------------------\n");
        printf("|CURRENT HEAP_LISTP POINTER :  %lu \n",  HEAP_LISTP_VAL);
        printf(" -------------------------------------\n");
        printf("|CURRENT BLOCK POINTER : %p\n", bp);
        printf("|bp[HDRP](size):  %d at %p\n", GET_SIZE(HDRP(bp)), HDRP(bp));
        printf("|bp[HDRP](alloc): %d at %p\n", get_alloc, HDRP(bp));
        /* Pointer to predecessor block*/
        printf("|bp[PREDPOINT]: %p at %p\n", PREDPOINT(bp), PRED(bp));
        /* Offset from heap_listp to predecessor block*/
        printf("|bp[PRED]: %u at %p\n", P_OFFSET_VAL(PREDPOINT(bp)), PRED(bp));
        /* Pointer to succesor block*/      
        printf("|bp[SUCCPOINT]: %p at %p\n", SUCCPOINT(bp), SUCC(bp));
        /* Offset from heap_listp to successor block*/
        printf("|bp[SUCC]: %u at %p\n", P_OFFSET_VAL(SUCCPOINT(bp)), SUCC(bp));
        printf("|bp[FTRP]: %d at %p\n", GET(FTRP(bp)), FTRP(bp));
        printf(" -------------------------------------\n");
    } else {
        printf("WARNING : Null pointer print called ! \n");
    }
}
#endif

/* ----------------------------------------------------------------------------
 * Function: check_cycle
 * Input parameters: head of linked list.
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description: 
 * Function to detect cyclic dependencies in a linked list. 
 * ----------------------------------------------------------------------------
 */
static void check_cycle (unsigned* head) {
    unsigned* slow = head;
    unsigned* fast = head;

    while ((slow!= NULL) && (fast!=NULL) && (SUCCPOINT(fast)!=NULL)) {
        slow = NEXTP(slow);
        fast = NEXTP(NEXTP(fast)); 
        if (slow == fast) {
            printf("%s Error: Cycle detected! \n",  __func__);
            exit(-1);
        }
        return;
    }
}

/* ----------------------------------------------------------------------------
 * Function: mm_checkheap
 * Input parameters: line number during call.
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description: 
 * The function is invoked to check the heap and free block consistency. It can
 * be invoked in two modes - verbose and silent. The verbose mode prints out 
 * all the information related to heap and free list status at the time of 
 * the call. The verbosity can be set by using the #define VERBOSE_CHECKHEAP.
 * Both, in the cases of silent and verbose, check_free_block and 
 * check_heap_block are called for checking their consistency.
 * 
 * This function was written with the specific intent of debugging this parti-
 * cular implementation and was of immense help during debugs ! The function
 * implementation evolved over implementation of implicit, explicit, segregated
 * (with 8 bytes first and 4 bytes later). 
 * ----------------------------------------------------------------------------
 * Note: The heap checker performs all the checks as required by the handout.
 * ----------------------------------------------------------------------------
 */
void mm_checkheap(int lineno) {
    int i = 0;
    char *bpc = NULL;            
    unsigned *bp = NULL;
    unsigned* head = NULL;
    int heap_free_count = 0; /* Counter of free allocations in heap */
    int freelist_free_count = 0; /* Counter of free allocations in freelist */

#ifdef VERBOSE_CHECKHEAP
    printf("**Checkheap at line %d** \n", lineno); 
    printf("Heap_listp (%p):\n", heap_listp);
    /*---------------------------------------------------------------*/
    /* Checking the Prologue header */
    if ((GET_SIZE(HDRP(heap_listp)) != DSIZE)||!GET_ALLOC(HDRP(heap_listp))){
        printf("%s Bad prologue header \n",  __func__);
        exit(-1);
    }

    /* Checking and printing all blocks in heap */
    /* Also counting number of free allocations in entire heap */
    printf("<<-- Printing all blocks in list -->>\n");
    check_heap_block(heap_listp);
    for (bpc=heap_listp; CURR_SIZE(bpc)>0; bpc=NEXT_BLKP(bpc)){
        print_heap_block(bpc);
        check_heap_block(bpc);
        if(GET_ALLOC(HDRP(bpc))==0) {
            heap_free_count++;
        }
    }
    print_heap_block(bpc);
    printf("<<-- End of all blocks in list -->>\n");

    /* Checking the Epilogue header */
    if ((GET_SIZE(HDRP(bpc)) != 0) || !(GET_ALLOC(HDRP(bpc)))){
        printf("%s Bad epilogue header \n",  __func__);
        exit(-1);
    }
    printf("**Exiting Checkheap at line %d ** \n", lineno);      
    /*---------------------------------------------------------------*/
    /* Checking and printing all blocks in current free list.
     * Also counting the number of free blocks in the list and verifying that 
     * a free block belongs to the correct segregated bin */   
      
    printf(">>-- Printing current free list --<<\n");
    for(i = 0 ; i<= (BIN_SIZE-1); i++){
        printf("Seglist number : [%d] \n", i);
        printf("Current seglist_head = %p \n", seglist_head[i]);
        head = seglist_head[i];
        if (head != NULL) {
                freelist_free_count++;
                check_cycle(head);  /* Checking for circular lists */
                for (bp = head; SUCCPOINT(bp)!=HEAP_NULL; bp = NEXTP(bp)) {
                    if(get_seg_index(GET_SIZE(HDRP(bp)))!= i){
                        printf("%s Wrong size in segregated list\n", __func__);
                        exit(-1);
                    }
                    freelist_free_count++;
                    print_free_block(bp);
                    check_free_block(bp);
                }
                print_free_block(bp);
                check_free_block(bp);
        } else {
            printf("No free list exists !\n");
        }
    }
    printf(">>-- End Current free list --<< \n");
    /*---------------------------------------------------------------*/
#else
    lineno = lineno;
    /* Checking the heap blocks for errors silently, same functions as above */
    if ((GET_SIZE(HDRP(heap_listp)) != DSIZE)||!GET_ALLOC(HDRP(heap_listp))){
        printf("%s Bad prologue header \n",  __func__);
        exit(-1);
    }
    check_heap_block(heap_listp);
    
    for (bpc = heap_listp; CURR_SIZE(bpc)>0; bpc= NEXT_BLKP(bpc)) {
        check_heap_block(bpc);
        if(GET_ALLOC(HDRP(bpc))==0) {
            heap_free_count++;
        }
    }

    if ((GET_SIZE(HDRP(bpc)) != 0) || !(GET_ALLOC(HDRP(bpc)))){
        printf("%s Bad epilogue header \n",  __func__);
        exit(-1);
    }

    for(i = 0 ; i<= (BIN_SIZE-1); i++){
        head = seglist_head[i];
        if (head != NULL) {
            check_cycle(head); 
            freelist_free_count++;
            for (bp = head; SUCCPOINT(bp)!=HEAP_NULL; bp = NEXTP(bp)) {
                if(get_seg_index(GET_SIZE(HDRP(bp)))!= i){
                    printf("%s Wrong size in segregated list\n", __func__);
                    exit(-1);
                }
                freelist_free_count++;
                check_free_block(bp);
            }
            check_free_block(bp);
        }
    }
#endif
    /* Exiting with error in case free counts don't match */
    if(heap_free_count != freelist_free_count) {
        printf("%s Free list counts don't match ! \n",  __func__);
        exit(-1);
    }
    return;
}
