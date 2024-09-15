#include "mygc.h"

static void *start_of_stack = NULL;

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

// represents one single list containing all the allocated blocks.
Block *alloc_block_end = NULL;

#define ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))

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
  // Clear the existing chunk index by ANDing with the inverse of the CHUNK_MASK
  block->markFlag_index &= ~CHUNK_MASK;

  // OR the chunk_index with the current index ensuring that the MARK_BIT is preserved
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

Block *get_last()
{
  Block *curr = alloc_block_end;
  // Traverse through the list of blocks using next_offset.
  while (curr)
  {
    Block *next_block = get_next_through_off(curr);
    if (!next_block)
    {
      // We reached the last block, return it
      return curr;
    }
    // Move to the next block
    curr = next_block;
  }
  return NULL; // If the list is empty, return NULL
}

/** Sets the offset of the from block to point to the to block. */
void set_next_off(Block *from, Block *to)
{
  if (to == NULL)
  {
    from->next_offset = 0; // Set to 0 if there's no valid 'to' block
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
  return block == ch_array[get_chunk_index(block)]->start;
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
    // curr = curr->next;
    prev = curr;
    // printf("The address of prev: %p\n", prev);
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
  set_next_off(block, NULL);
}

/** Removes the block from the alloc list
 * Does not change the allocation status
 * Also does nothing if block is not in the alloc list (safety check)
 */
void remove_from_alloc_list(Block *block)
{
  if (!block)
    return;

  // Find if the block is in the alloc list.
  // int index = calculate_index(get_size(block));
  Block *curr = alloc_block_end;
  Block *prev = NULL;
  while (curr != NULL && curr != block)
  {
    // curr = curr->next;
    prev = curr;
    // printf("The address of prev: %p\n", prev);
    curr = get_next_through_off(curr);
  }

  // If block is not found in the alloc list, return early
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
    alloc_block_end = next;
  }
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
  // next and prev could be safely set to null as would be taken care of in freeing
  // block1->next = NULL;
  set_next_off(block1, NULL);
  // block1->prev = NULL;
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

  // if (first_in_list != NULL)
  // {
  //   first_in_list->prev = block;
  // }

  // block->next = first_in_list;
  set_next_off(block, first_in_list); 
  // block->prev = NULL;

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

  // block->next = NULL;
  set_next_off(block, NULL);
  // block->prev = NULL;
  // block->index = index_last_chunk_from_OS;
  set_chunk_index(block, index_last_chunk_from_OS);
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
  return ch_array[get_chunk_index(block)];
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
  // right->index = get_chunk_index(block);
  set_chunk_index(right, get_chunk_index(block));
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
    // curr = curr->next;
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

// Call this function in your test code (at the start of main)
void set_start_of_stack(void *start_addr) {
  start_of_stack = start_addr;
}

/* Returns true if the given block could be reached */
bool is_marked(Block *block)
{
  return block->markFlag_index & MARK_BIT;
}

/* Marks a block as unreachable(in use) **for gc** */
void unmark_block(Block *block)
{
  block->markFlag_index &= ~MARK_BIT;
}

/** Given a pointer which might be in the middle of the block, will locate the block */
Block *find_block(void *ptr)
{
  for (int i = 0; i <= index_last_chunk_from_OS; i++)
  {
    Block *block = ch_array[i]->start;
    while ((void *)block < ch_array[i]->end && block)
    {
      // void *start = (void *)(block + 1); // Start of user data
      void *start = (void *)block; //
      void *end = ADD_BYTES(block, get_size(block));
      if (ptr >= start && ptr < end)
      {
        return block;
      }
      block = get_next_block(block); // Move to next block
    }
  }
  return NULL;
}

/** Marks the given block in use
 * Also recurses the entire block to find any other potential pointers
 */
void mark_block(Block *block)
{
  if (block == NULL || is_marked(block))
  {
    return; // Already marked or null
  }

  // Mark the block
  block->markFlag_index |= MARK_BIT;

  // Scan the contents of the block for any pointers
  uintptr_t *block_data = (uintptr_t *)(block + 1); // Skip the block metadata

  size_t block_size = get_size(block);
  for (size_t i = 0; i < block_size / sizeof(uintptr_t); i++)
  {
    void *potential_ptr = (void *)block_data[i];
    Block *inner_block = find_block(potential_ptr);
    if (!inner_block)
    {
      mark_block(inner_block); // Recursively mark inner blocks
    }
  }
}

/** Scans the stack and if word aligned addresses are found, marks them as in use */
void scan_and_mark(void *stack_bottom)
{
  // Loop through word-aligned memory on the stack
  void **stack_ptr = (void **)stack_bottom;

  while ((uintptr_t)stack_ptr < (uintptr_t)start_of_stack)
  {
    Block *potential_block = find_block(*stack_ptr);
    if (potential_block)
    {
      mark_block(potential_block);
    }
    stack_ptr++;
  }
}

/** Removes all the unmarked blocks to free memory. */
void sweep()
{
  // for (int i = 0; i <= index_last_chunk_from_OS; i++)
  // {
    // Block *block = ch_array[i]->start;
    Block *block = alloc_block_end;
    Block *freed = NULL;
    while (block)
    {
      if (!is_marked(block))
      {
        // Free the block if it's not marked
        
        // my_free(block + 1); //free the ptr returned by mymalloc
        freed = block;
        
      }
      block = get_next_through_off(block); //TODO: COULD BE MADE MORE EFFECTIVE
      remove_from_alloc_list(freed);
      my_free(freed + 1);
    }
  // }
}

void *my_malloc(size_t size) 
{
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
      // Chunk *chunk = get_chunk_from_OS((total_size + sizeof(FENCEPOST) + sizeof(Chunk) + kMemorySize - 1) / kMemorySize);
      get_chunk_from_OS((total_size + sizeof(FENCEPOST) + sizeof(Chunk) + kMemorySize - 1) / kMemorySize);
    }
    // int possible_index = calculate_index(total_size);
    Block *best_fit_block = best_fit(total_size);

    if (!best_fit_block)
    {
      get_chunk_from_OS((total_size + kMemorySize - 1) / kMemorySize);
      best_fit_block = list_fit(free_lists[N_LISTS - 1], total_size);
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
    if (!alloc_block_end) {
      alloc_block_end = best_fit_block;
    } else {
      set_next_off(get_last(alloc_block_end), best_fit_block);
    }
    // unmark_block(best_fit_block);
    return (void *)(best_fit_block + 1);
  }
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
  // put_block_in_free_list(block, calculate_index(get_size(block))); //this can be an issue
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

void *get_end_of_stack() {
  return __builtin_frame_address(1);
}

void my_gc() {
  void *end_of_stack = get_end_of_stack();

  // TODO
  scan_and_mark(end_of_stack);
  sweep();
}

/* Returns the size of the given block */
size_t block_size(Block *block)
{
  return get_size(block);
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
      printf(" Block at address %p | Size: %zu | %s\n", (void *)(curr), size, allocated ? "Allocated" : "Free");

      // Move to the next block
      curr = get_next_block(curr);
    }
  }

  printf("-------------------\n");
}