#include "mymalloc.h"

// Word alignment
const size_t kAlignment = sizeof(size_t);
// Minimum allocation size (1 word)
const size_t kMinAllocationSize = kAlignment;
// Size of meta-data per Block
const size_t kMetadataSize = sizeof(Block);
// Maximum allocation size (128 MB)
const size_t kMaxAllocationSize = (128ull << 20) - kMetadataSize;
// Memory size that is mmapped (64 MB)
const size_t kMemorySize = (64ull << 20);

Chunk *first_map = NULL;

/** Returns a pointer to the last (most recent) memory chunk
 * that was requested from OS.
 * Would return NULL if memory was never requested.
 */
Chunk *last_chunk_from_OS()
{
  Chunk *last = first_map;
  if (last != NULL)
  {
    while (last->next != NULL)
    {
      last = last->next;
    }
  }
  return last;
}

void *my_malloc(size_t size) {
  return NULL;
}

void my_free(void *ptr) {
  return;
}

/** These are helper functions you are required to implement for internal testing
 *  purposes. Depending on the optimisations you implement, you will need to
 *  update these functions yourself.
 **/

/* Returns 1 if the given block is free, 0 if not. */
int is_free(Block *block) {
  return !block->allocated;
}

/* Returns the size of the given block */
size_t block_size(Block *block) {
  return block->size;
}

/* Returns the first block in memory (excluding fenceposts) */
Block *get_start_block(void) {
  return NULL;
}

/* Returns the next block in memory */
Block *get_next_block(Block *block) {
  return NULL;
}

/* Given a ptr assumed to be returned from a previous call to `my_malloc`,
   return a pointer to the start of the metadata block. */
Block *ptr_to_block(void *ptr) {
  return ADD_BYTES(ptr, -((ssize_t) kMetadataSize));
}
