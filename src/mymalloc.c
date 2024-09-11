#include "mymalloc.h"
#include "../tests/testing.h"
#include <string.h>

//MARK AS WORKING
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
// Maximum number of requests from OS
#define MAX_REQ 256

#define ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))

size_t round_down(size_t size, size_t alignment)
{
  return size & ~(alignment - 1);
}

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

/** Returns a pointer to the last (most recent) memory chunk
 * that was requested from OS.
 * Would return NULL if memory was never requested.
 */
// Chunk *last_chunk_from_OS()
// {
// Chunk *last = first_map;
// if (last != NULL)
// {
//   while (last->next != NULL)
//   {
//     last = last->next;
//   }
// }
// return last;
// }

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
 * allocated blocks does not have a footer
 */
Footer *get_footer(Block *free_block)
{
  Footer *footer = (Footer *)ADD_BYTES(free_block, block_size(free_block) - sizeof(Footer));
  return footer;
}

/* Checks whether moving to the left takes us out of the chunk. */
bool is_prev_outside_chunk(Block *block)
{
  // Get the address of the potential fencepost (which is a uint32_t value).
  // uint32_t *possible_fencepost = (uint32_t *)ADD_BYTES(block, -sizeof(Chunk));

  // Check if the previous block is a fencepost.
  return block==ch_array[block->index]->start;
}

/* Checks whether the block next to the given block falls out of the chunk to which thegivenblock belongs */
bool is_next_outside_chunk(Block *block)
{
  // Get the address of the potential fencepost (which is a uint32_t value).
  Block *next = get_next_block(block);
  if (!next || (void *)next >= ch_array[block->index]->end)
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
  while (curr != NULL && curr != block)
  {
    curr = curr->next;
  }

  // If block is not found in the free list, return early
  if (curr == NULL)
  {
    return;
  }

  Block *prev = block->prev;
  Block *next = block->next;

  // If there's a previous block, update its next pointer.
  if (prev != NULL)
  {
    prev->next = next;
  }
  else
  {
    // Handle case where the block is the head of the free list.
    free_lists[index] = next;
  }

  // If there's a next block, update its prev pointer.
  if (next != NULL)
  {
    next->prev = prev;
  }

  // Clear the block's next and prev pointers.
  block->next = NULL;
  block->prev = NULL;
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
  // next and prev could be safely set to null as would be taken care of in freeing
  block1->next = NULL;
  block1->prev = NULL;
  get_footer(block1)->size = get_size(block1);
  get_footer(block1)->allocated = 0;
}

/** Puts the given block in the appropriate free list (no check for neighbouring blocks for coalescing).
 *  Assumes status has been set already
 * Assumes that the given block is already coalesced.
 */
void put_block_in_free_list(Block *block, int index)
{
  // int index = calculate_index(get_size(block));
  // Insert the coalesced block at the head of the free list
  if (index >= N_LISTS)
  {
    index = N_LISTS - 1;
  }
  Block *first_in_list = free_lists[index];
  // set_allocation_status(first_in_list, 0, 1);

  if (first_in_list != NULL)
  {
    first_in_list->prev = block;
  }

  block->next = first_in_list;
  block->prev = NULL;

  // Update the head of the free list
  free_lists[index] = block;
  // set_allocation_status(first_in_list, 0, 0);
}

/** Puts the block in the appropriate free list mainly taking care of logic coalescing
 * Also takes care of coalescing
 * assumes block has updated alocation status as free
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

  block->next = NULL;
  block->prev = NULL;
  block->index = index_last_chunk_from_OS;
  // Navigate to the last of the chunk to place a fencepost at the other end too
  // uint32_t *end_fencepost = ADD_BYTES(chunk, multiple * kMemorySize - sizeof(FENCEPOST));
  // *end_fencepost = FENCEPOST;
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
  return ch_array[block->index];
}

/** Splits a free block starting at the given address into two free blocks
 * Assumes size includes metadata size
 * Returns the one which is higher in memory
 */
Block *split_block(Block *block, size_t size)
{
  // remove_from_free_list(block);
  size_t original_size = get_size(block);
  set_size(block, original_size - size);
  Block *right = get_next_block(block);
  // printf("Block right's address after splitting :%p \n", right);
  right->index = block->index;
  set_size(right, size);
  set_allocation_status(block, 0, 1);
  set_footer(block);
  // put_block_in_free_list(block);

  set_allocation_status(right, 0, 1);
  set_footer(right);
  // put_block_in_free_list(right);

  set_allocation_status(right, 0, 0);

  return right;
}

/** Given a chunk, finds a block in that having size closest to size.
 * size is inclusive of meta-data size
 */
// Block *best_fit_in_chunk(Chunk *chunk, size_t size)
// {
//   Block *search = chunk->start_free_list;
//   Block *best = NULL;
//   size_t diff = SIZE_MAX;
//   while (search != NULL)
//   {
//     size_t calc_diff = search->size - size;
//     if (calc_diff >= 0 && calc_diff < diff)
//     {
//       best = search;
//       diff = calc_diff;
//     }
//     search = search->next;
//   }
//   return best;
// }

/* Returns a block by searching the list starting at base if the block exists, NULL otherwise*/
Block *list_fit(Block *base, size_t size)
{
  Block *best = NULL;
  Block *curr = base;
  size_t diff = SIZE_MAX; // Keep this as size_t to track the smallest suitable block

  while (curr)
  {
    ssize_t calc_diff = get_size(curr) - size;
    // printf("Checking block size: %zu, requested size: %zu, calc_diff: %zd\n", get_size(curr), size, calc_diff);
    // printf("Address of block: %p\n", curr);

    // Ensure the block is large enough and has the smallest diff (best fit)
    if (calc_diff >= 0 && (size_t)calc_diff < diff) // Ensure block is large enough and smaller than current best
    {
      best = curr;
      diff = (size_t)calc_diff; // Update diff with the current smallest difference
    }
    curr = curr->next;
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

/* Removes a block to the appropriate */
// void mallocing(Block *block)
// {
//   Chunk *assoc_chunk = chunk_from_block(block);
//   Block *prev_in_free_list = block->prev;
//   Block *next_in_free_list = block->next;
//   prev_in_free_list->next = next_in_free_list;
//   if (next_in_free_list)
//   {
//     next_in_free_list->prev = prev_in_free_list;
//   }
//   Block *alloc_before = assoc_chunk->start_alloc_list;
//   if (!alloc_before)
//   {
//     assoc_chunk->start_alloc_list = block;
//     block->next = NULL;
//     block->prev = NULL;
//     return;
//   }
//   while (alloc_before < block)
//   {
//     alloc_before = alloc_before->next;
//   }
//   Block *alloc_after = alloc_before->prev;
//   block->next = alloc_before;
//   block->prev = alloc_after;
// }

/* Returns a pointer to the block of memory satisfying size. */
void *my_malloc(size_t size)
{
  if (!size)
  {
    return NULL;
  }
  if (size == 0)
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
    Chunk *chunk = get_chunk_from_OS((total_size + sizeof(FENCEPOST) + sizeof(Chunk) + kMemorySize - 1) / kMemorySize);
  }
  // int possible_index = calculate_index(total_size);
  Block *best_fit_block = best_fit(total_size);

  if (!best_fit_block)
  {
    get_chunk_from_OS((total_size + kMemorySize - 1) / kMemorySize);
    best_fit_block = list_fit(free_lists[N_LISTS], total_size);
  }
  // printf("The val of checking fence: %i\n", is_next_fencepost(best_fit_block));
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
    // set_allocation_status(best_fit_block, 0, 0);
    // The previous block is still free
    // set_allocation_status(best_fit_block, 0, 0);
    Footer *prev_footer = (Footer *)ADD_BYTES(best_fit_block, -sizeof(Footer));
    Block *prev_contiguous_block = (Block *)ADD_BYTES(best_fit_block, -prev_footer->size);
    freeing_up(prev_contiguous_block);
    // remove_from_free_list(best_fit_block);
  }

  return (void *)(best_fit_block + 1);
}

bool is_valid(void *ptr)
{
  // free called when memory allocator has not been initialised
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
  // if (get_size(block) == 0)
  // {
  //   return;
  // }
  set_allocation_status(block, 0, 1);
  set_footer(block);
  size_t chunk_size = corr_size_arr[block->index];
  if (get_next_block(block) < (Block *)ADD_BYTES(ch_array[block->index], chunk_size))
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
  size_t chunk_size = corr_size_arr[block->index];
  // printf("Index of the chunk: %i\n", block->index);
  if (next_block >= (Block *)ADD_BYTES(ch_array[block->index], chunk_size))
  {
    if (block->index + 1 < MAX_REQ && ch_array[block->index + 1])
    {
      // Move to the next chunk's start
      return ch_array[block->index + 1]->start;
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
      // Get the allocation status
      bool allocated = is_allocated(curr);
      size_t size = get_size(curr);
      printf("  Block at address %p | Size: %zu | %s\n", (void *)curr, size, allocated ? "Allocated" : "Free");

      // Move to the next block
      curr = get_next_block(curr);
    }
  }

  printf("-------------------\n");
}

void print_chunk(void)
{
  printf("---- Chunk Dump ----\n");

  for (int i = 0; i <= index_last_chunk_from_OS; i++)
  {
    Chunk *chunk = ch_array[i];
    size_t chunk_size = corr_size_arr[i];

    // Print the chunk details
    printf("Chunk %d (address: %p):\n", i, (void *)chunk);
    printf("  Chunk Size: %zu bytes\n", chunk_size);
    printf("  Start Block Address: %p\n", (void *)chunk->start);
    printf("  Fencepost Address: %p\n", (void *)ADD_BYTES(chunk, chunk_size - sizeof(FENCEPOST)));
    printf("  Fencepost Value: %x\n", FENCEPOST);

    Block *curr_block = chunk->start;
    while ((void *)curr_block < ADD_BYTES(chunk, chunk_size) && curr_block)
    {
      // Get block details
      bool allocated = is_allocated(curr_block);
      size_t block_size = get_size(curr_block);

      // Print block details
      printf("  Block at address %p | Size: %zu | %s\n",
             (void *)curr_block, block_size, allocated ? "Allocated" : "Free");

      // Move to the next block
      curr_block = get_next_block(curr_block);
    }
  }

  printf("---------------------\n");
}

#include <stdint.h>
#include <stdio.h>



int main(void)
{
  int *mem1 = (int *)mallocing(sizeof(int));
  *mem1 = 10;
  return *mem1 - *mem1;
}
