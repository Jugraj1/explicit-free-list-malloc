#include "mymalloc-raw.h"
#include "../tests/testing.h"

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
void *get_chunk_from_OS(int multiple)
{
    Chunk *chunk = mmap(NULL, multiple * kMemorySize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    if (chunk == MAP_FAILED)
    {
        errno = ENOMEM;
        return NULL;
    }
    chunk->fencepost = FENCEPOST;
    // Navigate to the last of the chunk to place a fencepost at the other end too
    uint32_t *end_fencepost = ADD_BYTES(chunk, kMemorySize - 4);
    *end_fencepost = FENCEPOST;
    Chunk *prev = last_chunk_from_OS();
    chunk->next = NULL;
    chunk->prev = prev;
    chunk->size = multiple * kMemorySize;

    // if (prev == NULL)
    // {
    //   first_map = chunk;
    // }
    chunk->start_free_list = (Block *)ALIGN_UP((uintptr_t)(chunk + 1), kAlignment);
    chunk->start_free_list->size = multiple * kMemorySize - ((uintptr_t)chunk->start_free_list - (uintptr_t)chunk);

    chunk->start_free_list->next = NULL;
    chunk->start_free_list->prev = NULL;
    // chunk->start_free_list->allocated = false;
    chunk->start_alloc_list = NULL;
    if (prev)
    {
        prev->next = chunk;
    }
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
    Chunk *suitable_chunk = first_map;
    while (block >= (Block *)ADD_BYTES(suitable_chunk, suitable_chunk->size))
    {
        suitable_chunk = suitable_chunk->next;
        if (!suitable_chunk)
        {
            return NULL;
        }
    }
    return suitable_chunk;
}

/** Note that this function only works if `alignment` is a power of 2. **/
static inline size_t round_up(size_t size, size_t alignment)
{
    const size_t mask = alignment - 1;
    return (size + mask) & ~mask;
}

/** Splits the block starting at the given address into two
 * Assumes size includes metadata size
 * Returns the one which is higher in memory
 */
Block *split_block(Block *block, size_t size)
{
    size_t original_size = block->size;
    block->size = original_size - size;

    Block *right = get_next_block(block);
    right->size = size;

    right->next = block->next;
    if (block->next)
    {
        block->next->prev = right;
    }
    block->next = right;
    right->prev = block;

    return right;
}

/** Given a chunk, finds a block in that having size closest to size.
 * size is inclusive of meta-data size
 */
Block *best_fit_in_chunk(Chunk *chunk, size_t size)
{
    Block *search = chunk->start_free_list;
    Block *best = NULL;
    size_t diff = SIZE_MAX;
    while (search != NULL)
    {
        size_t calc_diff = search->size - size;
        if (calc_diff >= 0 && calc_diff < diff)
        {
            best = search;
            diff = calc_diff;
        }
        search = search->next;
    }
    return best;
}

/* Returns the smallest suitable free block by searching all available chunks. */
Block *best_fit(size_t size)
{
    Block *best = NULL;
    Chunk *curr_chunk = first_map;

    while (curr_chunk != NULL)
    {
        Block *current_best_in_chunk = best_fit_in_chunk(curr_chunk, size);

        if (current_best_in_chunk)
        {
            if (!best || (current_best_in_chunk->size < best->size))
            {
                best = current_best_in_chunk;
            }
        }

        curr_chunk = curr_chunk->next;
    }

    return best;
}

/* Transfers block from free list to allocated list of that particular chunk. */
/* Transfers block from the free list to the allocated list of that particular chunk. */
void mallocing_up(Block *block)
{
    if (!block)
    {
        return; // Block is null, cannot proceed
    }

    // Get the chunk associated with the block
    Chunk *assoc_chunk = chunk_from_block(block);
    if (!assoc_chunk)
    {
        return; // Invalid chunk, cannot proceed
    }

    // Remove the block from the free list
    Block *prev_in_free_list = block->prev;
    Block *next_in_free_list = block->next;

    // Check if block is the first in the free list
    if (prev_in_free_list)
    {
        prev_in_free_list->next = next_in_free_list;
    }
    else
    {
        // If block was the first block in the free list, update the chunk's start pointer
        assoc_chunk->start_free_list = next_in_free_list;
    }

    // Check if block is not the last in the free list
    if (next_in_free_list)
    {
        next_in_free_list->prev = prev_in_free_list;
    }

    // Now, insert the block into the allocated list of the chunk
    Block *alloc_before = assoc_chunk->start_alloc_list;

    // If allocated list is empty, this block becomes the first element
    if (!alloc_before)
    {
        assoc_chunk->start_alloc_list = block;
        block->next = NULL;
        block->prev = NULL;
        return;
    }

    // Traverse the allocated list to find the correct position for insertion (sorted order)
    while (alloc_before && alloc_before < block)
    {
        alloc_before = alloc_before->next;
    }

    // Find the block that comes before this block in the allocated list
    Block *alloc_after = alloc_before ? alloc_before->prev : NULL;

    // Insert the block into the allocated list
    block->next = alloc_before;
    block->prev = alloc_after;

    // Update the previous block's next pointer if necessary
    if (alloc_after)
    {
        alloc_after->next = block;
    }
    else
    {
        // If there is no previous block, this block becomes the head of the allocated list
        assoc_chunk->start_alloc_list = block;
    }

    // Update the next block's previous pointer if necessary
    if (alloc_before)
    {
        alloc_before->prev = block;
    }
}

int is_fencepost(Block *block)
{
    // Assuming fenceposts are special markers with a unique size or flag
    return block->size == FENCEPOST;
}

/* Transfers block from the allocated list to the free list of that particular chunk. */
void freeing_up(Block *block)
{
    if (!block)
    {
        return; // Block pointer is null, nothing to free
    }

    // Get the chunk associated with this block
    Chunk *assoc_chunk = chunk_from_block(block);
    if (!assoc_chunk)
    {
        return; // Invalid chunk, cannot proceed
    }

    // Ensure block is not a fencepost
    if (is_fencepost(block))
    {
        return; // Fencepost should not be freed
    }

    // Remove block from the allocated list
    Block *prev_in_alloc_list = block->prev;
    Block *next_in_alloc_list = block->next;

    // Check if block is the first in the allocated list
    if (prev_in_alloc_list)
    {
        prev_in_alloc_list->next = next_in_alloc_list;
    }

    // Check if block is the last in the allocated list
    if (next_in_alloc_list)
    {
        next_in_alloc_list->prev = prev_in_alloc_list;
    }
}

/* Returns a pointer to the block of memory satisfying size. */
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
    
    size_t total_size = round_up(size + sizeof(Block), kAlignment);
    if (!first_map)
    {
        first_map = get_chunk_from_OS((total_size + sizeof(Chunk) + kMemorySize - 1) / kMemorySize);
    }
    Block *best_fit_block = best_fit(total_size);
    while (!best_fit_block)
    {
        best_fit_block = best_fit(total_size);
        if (!best_fit_block)
        {
            Block *new_chunk = get_chunk_from_OS((total_size + sizeof(Chunk) + kMemorySize - 1) / kMemorySize);
            if (!new_chunk)
            {
                return NULL; // OS failed to provide more memory
            }
        }
    }
    if (best_fit_block->size == total_size || (best_fit_block->size - total_size) < (kMinAllocationSize + sizeof(Block)))
    {
        mallocing_up(best_fit_block);
    }
    else
    {
        best_fit_block = split_block(best_fit_block, total_size);
        mallocing_up(best_fit_block);
    }
    return (void *)(best_fit_block + 1);
}

/** merges block1 and block2 into one single block (effective in block1)
 * block1 and block2 must be adjacent
 * Assumes that block1 appears before block2
 */
void join(Block *block1, Block *block2)
{
    block1->size = block1->size + block2->size;
    block1->next = block2->next;

    if (block1->next)
    {
        block1->next->prev = block1;
    }
}



void my_free(void *ptr)
{
    if (!ptr)
    {
        return;
    }

    // Convert the pointer to a Block
    Block *block = ptr_to_block(ptr);
    if (!block)
    {
        return;
    }

    // Get the chunk that contains this block
    Chunk *chunk = chunk_from_block(block);
    if (!chunk)
    {
        return;
    }

    // Ensure the block is actually allocated and is not a fencepost
    if (is_free(block) || is_fencepost(block))
    {
        return; // Block is already free or it's a fencepost
    }

    // Remove the block from the allocated list
    freeing_up(block);

    // Insert the block into the free list
    Block *start_free_list = chunk->start_free_list;
    if (!start_free_list)
    {
        chunk->start_free_list = block;
        block->next = NULL;
        block->prev = NULL;
    }
    else
    {
        // Insert block into free list, maintaining sorted order
        Block *current = start_free_list;
        while (current && (current < block))
        {
            current = current->next;
        }
        if (current)
        {
            Block *prev = current->prev;
            block->next = current;
            block->prev = prev;
            if (prev)
            {
                prev->next = block;
            }
            current->prev = block;
            if (current == start_free_list)
            {
                chunk->start_free_list = block;
            }
        }
        else
        {
            // Insert at the end of the list
            Block *last = chunk->start_free_list;
            while (last->next)
            {
                last = last->next;
            }
            last->next = block;
            block->prev = last;
            block->next = NULL;
        }
    }

    // Try to merge the block with adjacent free blocks, but skip over fenceposts
    Block *next_block = get_next_block(block);
    if (next_block && is_free(next_block) && !is_fencepost(next_block))
    {
        join(block, next_block);
    }

    Block *prev_block = block->prev;
    if (prev_block && is_free(prev_block) && !is_fencepost(prev_block))
    {
        join(prev_block, block);
    }
}

/** These are helper functions you are required to implement for internal testing
 *  purposes. Depending on the optimisations you implement, you will need to
 *  update these functions yourself.
 **/

/** Returns 1 if the given block is free, 0 if not.
 *  The block must be in the given chunk.
 **/
int is_free(Block *block)
{
    if (!block)
    {
        return 0;
    }
    Chunk *chunk = chunk_from_block(block);
    Block *curr = chunk->start_free_list;
    while (curr != NULL)
    {
        if (curr == block)
        {
            return 1;
        }
        curr = curr->next;
    }
    return 0;
}

/* Returns the size of the given block */
size_t block_size(Block *block)
{
    return block->size;
}

/** Given a Chunk, this function would return first allocable block (excluding fenceposts & meta-deta of chunk)
 */
Block *get_start_block_in_chunk(Chunk *chunk)
{
    if (chunk == NULL)
    {
        return NULL;
    }
    Block *start_alloc = chunk->start_alloc_list;
    Block *start_free = chunk->start_free_list;
    if (!start_alloc)
    {
        return start_free;
    }
    else if (!start_free)
    {
        return start_alloc;
    }
    if (start_free < start_alloc)
    {
        return start_free;
    }
    return start_alloc;
}

/* Returns the first block in memory (excluding fenceposts) */
Block *get_start_block(void)
{
    return get_start_block_in_chunk(first_map);
}

/* Returns the next block in memory */
Block *get_next_block(Block *block)
{
    if (!block)
    {
        return NULL;
    }
    Chunk *chunk_of_block = chunk_from_block(block);
    Block *next_block = ADD_BYTES(block, block_size(block));
    size_t chunk_size = chunk_of_block->size;
    if (next_block >= (Block *)ADD_BYTES(chunk_of_block, chunk_size))
    {
        if (chunk_of_block->next)
        {
            return get_start_block_in_chunk(chunk_of_block->next);
        }
        return NULL;
    }
    return next_block;
}

/* Given a ptr assumed to be returned from a previous call to `my_malloc`,
   return a pointer to the start of the metadata block. */
Block *ptr_to_block(void *ptr)
{
    return ADD_BYTES(ptr, -((ssize_t)kMetadataSize));
}