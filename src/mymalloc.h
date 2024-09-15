#ifndef MYMALLOC_HEADER
#define MYMALLOC_HEADER

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>

#ifdef ENABLE_LOG
#define LOG(...) fprintf(stderr, "[malloc] " __VA_ARGS__);
#else
#define LOG(...)
#endif

#define N_LISTS 59

#define ADD_BYTES(ptr, n) ((void *) (((char *) (ptr)) + (n)))

#define FENCEPOST 0xDEADBEEF

// Second bit for previous block alloc status
#define PREV_FREE_MASK 0x2
// First bit for current block allocation status
#define ALLOC_MASK 0x1
// Mask to extract size
#define SIZE_MASK (~(PREV_FREE_MASK | ALLOC_MASK))
// Use the most significant bit for the mark flag
#define MARK_BIT (1 << 31)
// Mask to get the chunk index
#define CHUNK_MASK (~MARK_BIT)
// Maximum number of requests from OS
#define MAX_REQ 256


/** This is the Block struct, which contains all metadata needed for your 
 *  explicit free list. You are allowed to modify this struct (and will need to 
 *  for certain optimisations) as long as you don't move the definition from 
 *  this file. **/
typedef struct Block Block;

struct Block {
  // Size of the block, including meta-data size.
  // Also stores two flags : allocation status of current (Least significant bit) and previous block (Second L.S.B)
  size_t size_and_flags;

  // Offset that will show how far is the next free block from this one (0 shows none)
  int32_t next_offset;

  // Next and Prev blocks
  // Block *next;
  // Block *prev;
  // Is the block allocated or not?
  // bool allocated;
  // Index to keep track of the associated Chunk
  int markFlag_index;
};


/** Represents an entire OS-level 'Chunk' of memory as returned from mmap.
 */
typedef struct Chunk Chunk;

struct Chunk
{
  // Fencepost to detect buffer overflows
  // uint32_t fencepost; 

  // Represents the start of the first block
  void *start;
  void *end; // This will be the address which is right after the last allocable block 

  // Chunk *next;
  // Chunk *prev;

  // This represents the start of free blocks in this memory returned by OS
  // Block *start_free_list;
  // This represents the start of allocated blocks in this memory returned by OS
  // Block *start_alloc_list;
};

// extern Chunk *ch_array[MAX_REQ];

/** Represents the footer for a block storing size and the allocated status
 */
typedef struct Footer Footer;

struct Footer
{
  size_t size;
  bool allocated;
};

// Word alignment
extern const size_t kAlignment;
// Minimum allocation size (1 word)
extern const size_t kMinAllocationSize;
// Size of meta-data per Block
extern const size_t kMetadataSize;
// Maximum allocation size (128 MB)
extern const size_t kMaxAllocationSize;
// Memory size that is mmapped (64 MB)
extern const size_t kMemorySize;

void *my_malloc(size_t size);
void my_free(void *p);

/* Helper functions you are required to implement for internal testing. */

int is_free(Block *block);
size_t block_size(Block *block);

Block *get_start_block(void);
Block *get_start_block_in_chunk(Chunk *chunk);
Block *get_next_block(Block *block);

Block *ptr_to_block(void *ptr);


#endif
