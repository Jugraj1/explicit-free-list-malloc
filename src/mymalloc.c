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


#define ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))

// Chunk *first_map = NULL;
Chunk *ch_array[MAX_REQ] = {NULL};
size_t corr_size_arr[MAX_REQ];

// An array of free lists, where each entry corresponds to a range of block sizes.
// The 0th item contains blocks for sizes 1 to 8 bytes.
// The i-th item contains blocks for sizes from (i * 8 + 1) to (i + 1) * 8 bytes.
Block *free_lists[N_LISTS];

// Global variable to track the last chunk
int index_last_chunk_from_OS = -1;

/** Note that this function only works if alignment is a power of 2. **/
static inline size_t round_up(size_t size, size_t alignment)
{
  const size_t mask = alignment - 1;
  return (size + mask) & ~mask;
}

/** Returns the appropriate index of a free list
 * that might contain a block of given size or bigger.
 */
int calculate_index(size_t size)
{
  int index = (size + kAlignment) / kAlignment - 1;
  if (index >= N_LISTS)
  {
    index = N_LISTS - 1;
  }
  return index;
}

/* Used to extract the chunk index. */
int get_chunk_index(Block *block)
{
  return block->markFlag_index & CHUNK_MASK;
}

void set_chunk_index(Block *block, int chunk_index)
{
  block->markFlag_index &= ~CHUNK_MASK;
  block->markFlag_index |= (chunk_index & CHUNK_MASK);
}

/* Calculates the size of the block. */
size_t get_size(Block *block)
{
  return block->size_and_flags & SIZE_MASK;
}

/* Sets the size of the given block while preserving other flags. */
void set_size(Block *block, size_t size)
{
  size_t flags = block->size_and_flags & ~SIZE_MASK; // Extract flag bits (excluding size).
  block->size_and_flags = size | flags;
}

/** Computes the next block in a free list.
 * Returns NULL if no more free block in that list
 */
Block *get_next_through_off(Block *block)
{
  if (!block || block->next_offset == 0)
  {
    return NULL;
  }
  return (Block *)((char *)block + block->next_offset);
}

/** Sets the offset of the from block to point to the to block. */
void set_next_off(Block *from, Block *to)
{
  if (to == NULL)
  {
    from->next_offset = 0;
  }
  else
  {
    from->next_offset = (int32_t)((char *)to - (char *)from);
  }
}

/* Checks whether the given block is allocated or not. */
bool is_allocated(Block *block)
{
  if (!block)
  {
    return true;
  }
  return block->size_and_flags & ALLOC_MASK;
}

/* Checks whether the previous block is allocated or not. */
bool is_prev_allocated(Block *block)
{
  return block->size_and_flags & PREV_FREE_MASK;
}

/** Sets or clears the allocation status of the current/prev block.
 * current = 1 means current block, current = 0 means previous block
 */
void set_allocation_status(Block *block, bool allocated, bool current)
{
  if (allocated && current)
  {
    block->size_and_flags |= ALLOC_MASK;
  }
  else if (allocated && !current)
  {
    block->size_and_flags |= PREV_FREE_MASK;
  }
  else if (!allocated && current)
  {
    block->size_and_flags &= ~ALLOC_MASK;
  }
  else
  {
    block->size_and_flags &= ~PREV_FREE_MASK;
  }
}

/* Sets the footer given a block. */
void set_footer(Block *block)
{
  Footer *footer = (Footer *)ADD_BYTES(block, block_size(block) - sizeof(Footer));
  footer->allocated = is_allocated(block);
  footer->size = get_size(block);
}

/** Gets the footer of the given block.
 * Function must be invoked only on free blocks,
 * allocated blocks does not have a footer (postponed to future implementations)
 */
Footer *get_footer(Block *free_block)
{
  Footer *footer = (Footer *)ADD_BYTES(free_block, block_size(free_block) - sizeof(Footer));
  return footer;
}

/* Checks whether moving to the left takes us out of the chunk. */
bool is_prev_outside_chunk(Block *block)
{
  return block==ch_array[get_chunk_index(block)]->start;
}

/* Checks whether the block next to the given block falls out of the chunk to which thegivenblock belongs */
bool is_next_outside_chunk(Block *block)
{
  // Get the address of the potential fencepost (which is a uint32_t value).
  Block *next = get_next_block(block);
  if (!next || (void *)next >= ch_array[get_chunk_index(block)]->end)
  {
    return true;
  }
  return false;
}

/** Removes the block from the appropriate free list
 * Does not change the allocation status
 * Also does nothing if block is not in any of the free list (safety check)
 */
void remove_from_free_list(Block *block)
{
  if (!block)
    return;

  // Find if the block is in the free list.
  int index = calculate_index(get_size(block));
  Block *curr = free_lists[index];
  Block *prev = NULL;
  while (curr != NULL && curr != block)
  {
    prev = curr;
    curr = get_next_through_off(curr);
  }

  // If block is not found in the free list, return early
  if (curr == NULL)
  {
    return;
  }

  Block *next = get_next_through_off(block);

  // If there's a previous block, update its next pointer.
  if (prev != NULL)
  {
      set_next_off(prev, next);
  }
  else
  {
    // Handle case where the block is the head of the free list.
    free_lists[index] = next;
  }
  // Clear the block's next pointer.
  set_next_off(block, NULL);
}

/** Simply coalesces two given blocks whilst also removing block1 from a list where it exists
 * Takes care of updating footer and every other information
 */
void coalesce(Block *block1, Block *block2)
{
  remove_from_free_list(block1);
  remove_from_free_list(block2);
  // Update the size of the block while preserving flag bits.
  set_size(block1, get_size(block1) + get_size(block2));
  // next could be safely set to null as would be taken care of in freeing
  set_next_off(block1, NULL);
  get_footer(block1)->size = get_size(block1);
  get_footer(block1)->allocated = 0;
}

/** Puts the given block in the appropriate free list (no check for neighbouring blocks for coalescing).
 *  Assumes status has been set already
 * Assumes that the given block is already coalesced.
 */
void put_block_in_free_list(Block *block, int index)
{
  // Insert the coalesced block at the head of the free list
  Block *first_in_list = free_lists[index];
  set_next_off(block, first_in_list);

  // Update the head of the free list
  free_lists[index] = block;
}

/** Puts the block in the appropriate free list mainly taking care of logic coalescing
 * Also takes care of coalescing
 * assumes block has updated allocation status as free
 */
void freeing_up(Block *block)
{
  bool prev_contiguous_allocated = is_prev_allocated(block);
  bool next_contiguous_allocated;
  bool next_fence = is_next_outside_chunk(block);
  if (next_fence)
  {
    next_contiguous_allocated = true;
  }
  else
  {
    next_contiguous_allocated = is_allocated(get_next_block(block));
  }

  if (!prev_contiguous_allocated && !is_prev_outside_chunk(block) && !next_fence && !next_contiguous_allocated)
  {
    // Calculate the previous block using its footer
    Footer *prev_footer = (Footer *)ADD_BYTES(block, -sizeof(Footer));
    Block *prev_contiguous_block = (Block *)ADD_BYTES(block, -prev_footer->size);
    Block *next_continguous_block = (Block *)ADD_BYTES(block, block_size(block));
    coalesce(prev_contiguous_block, block);
    coalesce(prev_contiguous_block, next_continguous_block);
    put_block_in_free_list(prev_contiguous_block, calculate_index(get_size(prev_contiguous_block)));
  }
  // Coalesce with the previous block
  else if (!prev_contiguous_allocated && !is_prev_outside_chunk(block))
  {
    // Calculate the previous block using its footer
    Footer *prev_footer = (Footer *)ADD_BYTES(block, -sizeof(Footer));
    Block *prev_contiguous_block = (Block *)ADD_BYTES(block, -prev_footer->size);
    coalesce(prev_contiguous_block, block);
    put_block_in_free_list(prev_contiguous_block, calculate_index(get_size(prev_contiguous_block)));
  }
  else if (!next_contiguous_allocated && !next_fence)
  {
    // Calculate the next block
    Block *next_contiguous_block = (Block *)ADD_BYTES(block, block_size(block));
    coalesce(block, next_contiguous_block);
    put_block_in_free_list(block, calculate_index(get_size(block)));
  }
  else
  {
    put_block_in_free_list(block, calculate_index(get_size(block)));
  }
}


/** Gets a chunk of a multiple of kMemorySize(= 64 MB) from the OS
 * This function should only be called when existing memory
 * can't satisfy needs or when it hasn't been requested once.
 * Sets the index_last_chunk_from_OS.
 * Returns NULL if OS fails, sets errno to ENOMEM.
 */
void *get_chunk_from_OS(int multiple)
{
  if (multiple < 1)
  {
    errno = EINVAL; // Invalid argument
    return NULL;
  }

  if (multiple > SIZE_MAX / kMemorySize)
  {
    errno = ENOMEM; // Overflow protection
    return NULL;
  }

  Chunk *chunk = mmap(NULL, multiple * kMemorySize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  if (chunk == MAP_FAILED)
  {
    errno = ENOMEM;
    return NULL;
  }
  index_last_chunk_from_OS += 1;
  
  Block *block = (Block *)ALIGN_UP((uintptr_t)ADD_BYTES(chunk, sizeof(Chunk)), kAlignment);
  chunk->start = block;
  set_size(block, round_up(multiple * kMemorySize - ((char *)block - (char *)chunk), kAlignment));
  chunk->end = ADD_BYTES(block, get_size(block));
  set_allocation_status(block, 0, 1);
  set_allocation_status(block, 1, 0);

  set_next_off(block, NULL);
  set_chunk_index(block, index_last_chunk_from_OS);
  ch_array[index_last_chunk_from_OS] = chunk;
  corr_size_arr[index_last_chunk_from_OS] = multiple * kMemorySize;
  set_footer(block);
  freeing_up(block);
  return chunk;
}

/** Given a block, returns the chunk to which the block belongs.
 */
Chunk *chunk_from_block(Block *block)
{
  if (!block)
  {
    return NULL;
  }
  return ch_array[get_chunk_index(block)];
}

/** Splits a free block starting at the given address into two free blocks
 * Assumes size includes metadata size
 * Returns the one which is higher in memory
 */
Block *split_block(Block *block, size_t size)
{
  size_t original_size = get_size(block);
  set_size(block, original_size - size);
  Block *right = get_next_block(block);
  set_chunk_index(right, get_chunk_index(block));
  set_size(right, size);
  set_allocation_status(block, 0, 1);
  set_footer(block);

  set_allocation_status(right, 0, 1);
  set_footer(right);

  set_allocation_status(right, 0, 0);

  return right;
}

/* Returns a block by searching the list starting at base if the block exists, NULL otherwise*/
Block *list_fit(Block *base, size_t size)
{
  Block *best = NULL;
  Block *curr = base;
  size_t diff = SIZE_MAX; // Keep this as size_t to track the smallest suitable block

  while (curr)
  {
    ssize_t calc_diff = get_size(curr) - size;

    // Ensure the block is large enough and has the smallest diff (best fit)
    if (calc_diff >= 0 && (size_t)calc_diff < diff)
    {
      best = curr;
      diff = (size_t)calc_diff;
    }
    curr = get_next_through_off(curr);
  }

  return best; // This will return a block that is >= requested size
}

/** Returns the smallest suitable free block by searching the appropriate free lists.
 * does not remove the block from the list
 */
Block *best_fit(size_t size)
{
  int index = calculate_index(size);
  Block *possible = list_fit(free_lists[index], size);
  if (!possible)
  {
    for (int i = index + 1; i < N_LISTS; i++)
    {
      possible = list_fit(free_lists[i], size);
      if (possible)
      {
        break;
      }
    }
  }
  return possible;
}

/* Returns a pointer to the block of memory satisfying size (payload). */
void *my_malloc(size_t size)
{
  if (!size)
  {
    return NULL;
  }
  if (size > kMaxAllocationSize)
  {
    return NULL;
  }
  size_t total_size = round_up(size + kMetadataSize + sizeof(Footer), kAlignment);
  if (index_last_chunk_from_OS == -1)
  {
    get_chunk_from_OS((total_size + sizeof(Chunk) + kMemorySize - 1) / kMemorySize);
  }
  Block *best_fit_block = best_fit(total_size);

  if (!best_fit_block)
  {
    get_chunk_from_OS((total_size + kMemorySize - 1) / kMemorySize);
    best_fit_block = list_fit(free_lists[N_LISTS - 1], total_size);
  }
  if (!is_next_outside_chunk(best_fit_block) && get_next_block(best_fit_block) != NULL)
  {
    set_allocation_status(get_next_block(best_fit_block), 1, 0);
  }
  if (get_size(best_fit_block) == total_size || get_size(best_fit_block) - total_size < (kMinAllocationSize + sizeof(Block) + sizeof(Footer)))
  {
    remove_from_free_list(best_fit_block);
    set_allocation_status(best_fit_block, 1, 1);
  }
  else
  {
    remove_from_free_list(best_fit_block);
    best_fit_block = split_block(best_fit_block, total_size);
    set_allocation_status(best_fit_block, 1, 1);
    set_footer(best_fit_block);
    Footer *prev_footer = (Footer *)ADD_BYTES(best_fit_block, -sizeof(Footer));
    Block *prev_contiguous_block = (Block *)ADD_BYTES(best_fit_block, -prev_footer->size);
    freeing_up(prev_contiguous_block);
  }

  return (void *)(best_fit_block + 1);
}

bool is_valid(void *ptr)
{
  if (index_last_chunk_from_OS < 0)
  {
    return false;
  }
  for (int i = index_last_chunk_from_OS; i >= 0; i--)
  {
    Chunk *curr = ch_array[i];
    size_t chunk_size = corr_size_arr[i];
    if ((void *)curr->start < (void *)ptr && (void *)ptr < (void *)ADD_BYTES(curr->start, chunk_size))
    {
      return true;
    }
  }
  return false;
}

// TODO: handle thw two cases mentioned in spec
void my_free(void *ptr)
{
  if (!ptr)
  {
    return;
  }
  if (!is_valid(ptr))
  {
    return;
  }
  Block *block = ptr_to_block(ptr);
  set_allocation_status(block, 0, 1);
  set_footer(block);
  size_t chunk_size = corr_size_arr[get_chunk_index(block)];
  if (get_next_block(block) < (Block *)ADD_BYTES(ch_array[get_chunk_index(block)], chunk_size))
  {
    if (!is_next_outside_chunk(block) && get_next_block(block) != NULL)
    {
      set_allocation_status(get_next_block(block), 0, 0);
    }
  }
  freeing_up(block);
}

/** These are helper functions you are required to implement for internal testing
 *  purposes. Depending on the optimisations you implement, you will need to
 *  update these functions yourself.
 **/

/** Returns 1 if the given block is free, 0 if not.
 **/
int is_free(Block *block)
{
  if (!block)
  {
    return 0;
  }

  // Check the allocation status flag
  return !is_allocated(block);
}

/* Returns the size of the given block */
size_t block_size(Block *block)
{
  return get_size(block);
}

/* Returns the first block in memory (excluding fenceposts) */
Block *get_start_block(void)
{
  return ch_array[0]->start;
}

/* Returns the next block in memory */
Block *get_next_block(Block *block)
{
  if (!block)
  {
    return NULL;
  }
  // Chunk *chunk_of_block = chunk_from_block(block);
  Block *next_block = ADD_BYTES(block, block_size(block));
  // It shiould be a multiple of kMemorySize, find a way to get its size
  size_t chunk_size = corr_size_arr[get_chunk_index(block)];
  // printf("Index of the chunk: %i\n", block->index);
  if (next_block >= (Block *)ADD_BYTES(ch_array[get_chunk_index(block)], chunk_size))
  {
    if (get_chunk_index(block) + 1 < MAX_REQ && ch_array[get_chunk_index(block) + 1])
    {
      // Move to the next chunk's start
      return ch_array[get_chunk_index(block) + 1]->start;
    }
    return NULL; // No more chunks
  }
  return next_block;
}

/* Given a ptr assumed to be returned from a previous call to my_malloc,
   return a pointer to the start of the metadata block. */
Block *ptr_to_block(void *ptr)
{
  return ADD_BYTES(ptr, -((ssize_t)kMetadataSize));
}

/* A very useful function for debugging*/
void print_heap(void)
{
  printf("---- Heap Dump ----\n");

  for (int i = 0; i <= index_last_chunk_from_OS; i++)
  {
    printf("Chunk %d (address: %p):\n", i, ch_array[i]);

    Block *curr = ch_array[i]->start;
    size_t chunk_size = corr_size_arr[i];
    while ((void *)curr < ADD_BYTES(ch_array[i], chunk_size) && curr)
    {
      bool allocated = is_allocated(curr);
      size_t size = get_size(curr);
      printf("  Block at address %p | Size: %zu | %s\n", (void *)curr, size, allocated ? "Allocated" : "Free");

      curr = get_next_block(curr);
    }
  }

  printf("-------------------\n");
}