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

/** Gets a constant chunk of kMemorySize(= 64 MB) from the OS
 * This function should only be called when existing memory
 * can't satisfy needs or when it hasn't been requested once.
 * Returns NULL if OS fails, sets errno to ENOMEM.
 */
void *get_chunk_from_OS(void)
{
  Chunk *chunk = mmap(NULL, kMemorySize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  if (chunk == MAP_FAILED) {
    errno = ENOMEM;
    return NULL;
  }
  chunk->fencepost = FENCEPOST;
  //Navigate to the last of the chunk to place a fencepost at the other end too
  uint32_t *end_fencepost = ADD_BYTES(chunk, kMemorySize - 4);
  *end_fencepost = FENCEPOST;
  Chunk *prev = last_chunk_from_OS();
  chunk->next = NULL;
  chunk->prev = prev;
  if (prev == NULL)
  {
    first_map = chunk;
  }
  chunk->start_free_list = (Block *)(chunk + 1);
  chunk->start_alloc_list = NULL;
  return chunk;
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

int main() {
  // Chunk *start = get_chunk_from_OS();
  // printf("Start address: %p", start);
}