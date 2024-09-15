# Report

<!-- You should write your report in this file. Remember to check that it's 
     formatted correctly in the pdf produced by the CI! -->

## Overview
This memory allocator implements an explicit free-list strategy, managing memory in distinct chunks that are requested from the operating system via mmap. The allocator maintains them in segregated free lists to optimize allocation and deallocation performance. Each free list contains blocks within a specific size range.

The primary data structure used is the Block struct, which includes metadata such as block size, allocation status, a 32-bit offset to the next free block (rather than a full pointer to reduce metadata overhead), mark-bit (for GC only) and an index denoting the chunk to which the block belongs.

The Chunk struct represents the virtual memory requested from mmap. It consists of two primary members: a pointer to the start of the first allocatable block and an end pointer, which is the address immediately following the last allocatable block.

**Optimization Highlights:**
+ **Segregated Free Lists:** The allocator organizes free blocks into segregated lists based on block size ranges. This optimizes the search time for finding a suitable block during allocation.
+ **Metadata Reduction:** The Block struct is carefully designed to minimize metadata overhead by storing a 32-bit offset for navigating between blocks, as opposed to full 64-bit pointers.
+ **Constant Time Coalescing:** Adjacent free blocks are coalesced in constant time during deallocation, minimizing fragmentation and improving memory reuse efficiency.

**High-level explanation of Best-Fit Allocation, Coalescing and Fenceposts**
The best-fit allocation strategy works by first calculating the index of the appropriate free list. This index is determined by the size of the block being requested. If a suitable block is found in the corresponding list, it is returned. If not, the allocator searches the free lists with larger size ranges, which guarantees blocks larger than the requested size. In cases where no suitable block is found due to allocation saturation, a new chunk is requested from the operating system, and a block from this chunk is returned.

Coalescing is achieved using an additional `Footer` struct, which stores the size and allocation status of blocks. When a block is freed, the allocator checks its neighboring blocks (both left and right) to determine if they are also free, enabling coalescing. This operation is performed in constant time. The footer of the previous block is used to quickly retrieve the size, and thus the start, of the previous block. For the next block, its location is computed by adding the size of the current block. The process includes boundary checks to ensure that the blocks being coalesced do not extend outside the memory chunk they belong to.

Fenceposts are implemented using the `Chunk` struct, which serves as logical boundaries for memory blocks. Each `Chunk` struct stores a pointer to the first allocable block and the address immediately after the last allocable block in the chunk. During coalescing, these fenceposts ensure that adjacent blocks are not mistakenly combined if they lie outside the boundaries of the current chunk. 

## Optimisations 

+ **Metadata Reduction**
     The `Block` struct was reduced from 32 bytes to 16 bytes, significantly optimizing memory usage across multiple allocations. The previous pointer was eliminated as it served no purpose in the allocator design, saving 8 bytes. The next pointer was replaced with a 32-bit relative offset (`int32_t`), representing the distance to the next block, either free or allocated, depending on the context. Size and allocation status were combined into a single field, using the two least significant bits to represent the current and previous block's allocation statuses. Additionally, the mark bit for garbage collection was incorporated into the chunk index, assuming the number of chunks remains manageable.

+ **Constant time coalescing with boundary tags**
     The allocator's time complexity for coalescing was optimized from linear to constant by utilizing footers (boundary tags). When freeing a block, the footer allows immediate access to the previous block’s size and allocation status, enabling efficient coalescing. This eliminates the need to traverse all blocks in the chunk to find the previous block, significantly reducing overhead. By checking both neighboring blocks in constant time, fragmentation is minimized and memory reuse is improved.

+ **Requesting additional chunks from the OS**
     When a `mymalloc` request cannot be fulfilled due to the lack of a suitable free block, a new memory chunk is requested from the OS using `mmap`. This chunk is added to an array (`ch_array`) that tracks the start addresses, and its size is stored in `corr_size_arr`. This strategy ensures fewer allocation failures when the current chunks are nearly full, allowing the allocator to dynamically scale memory. By maintaining chunk metadata, the system can efficiently distinguish between multiple chunks, improving the allocator’s flexibility and reducing allocation bottlenecks.

+ **Multiple Free Lists**
     To improve allocation efficiency, Multiple free lists categorized by block size ranges were introduced. Instead of searching through a single list for a suitable block, the allocator first calculates the appropriate list index based on the requested size. This index directly corresponds to a list of blocks within a predefined size range, significantly reducing search time. If no block is found in the corresponding list, the allocator checks larger block lists before requesting a new chunk from the OS if necessary. This optimization minimizes allocation time by narrowing the search scope, resulting in faster allocation for frequently requested block sizes and reducing fragmentation.

The next_offset having two fold uses: if the particular block is free, next_off refers to the offset of the next free block while if the block is allocated, next_off aims to make it a part of the allocated list, however this only reflects in the my_malloc fucntion in my_gc, ttraction of alloc blocks were of no use in the basic my_malloc. Allocation status of the current block, allocation status of the previous block and size has been combined into size_and_flags member. Similarly, markFlag_index represents whether the block is in use and also the index of the chunk to which the block belongs. however, note that mark flag is only to be used for allocated blocks and may have strange behaviour (not set) for free blocks. The chunk does not contain the size instead was included in a list which helps in saving memory when allocating blocks of small sizes.

## Garbage Collector Overview
The implemented garbage collector operates in two phases: mark and sweep. During the mark phase, the stack is scanned for word-aligned values that may represent valid memory addresses. Each of these values is cross-referenced with the list of allocated blocks, and if a match is found, the block is marked as in use. This process ensures that all reachable memory from the stack is retained. In the sweep phase, the allocator traverses all blocks and frees those that are unmarked, reclaiming unused memory. 
**Changes to the metadata**
     To avoid additional overhead, the garbage collector leverages the existing metadata structure. The most significant bit of the block index (identifies the chunk a block belongs to), was repurposed for marking during garbage collection. This 32-bit integer had sufficient unused space, making it a practical optimization without expanding the memory footprint.  

Implementing a garbage collector in C presents several challenges due to the language's low-level nature. One significant problem is that C does not natively distinguish between pointers and integers, making it difficult for the garbage collector to identify what is genuinely a pointer and what is just data. This ambiguity can lead to either missed memory that should be freed or mistakenly freed memory that is still in use. Additionally, manual memory management in C does not inherently track object references, making it hard to automate.

Garbage collectors generally require more support from the language itself because they rely on information about object lifetimes and references, which is easier to manage in higher-level languages with built-in memory management systems (such as Java or Python). These languages often come with predefined mechanisms like object tracking, type safety, and reference management, which reduce ambiguity and assist in efficiently managing memory. C lacks these abstractions, meaning implementing a garbage collector involves significant manual overhead and complexity.

## Testing
**Difficult Bug: Memory Corruption**
During the implementation, I encountered a challenging bug with the commit `5d2a5b7c`. The idea was to only set footers for free blocks to reduce metadata overhead. While this seemed efficient, it led to a memory corruption issue that took two days to debug. Upon allocating four blocks, `print_heap()` only showed two as allocated, leading me to suspect a bug in `get_next_block()` since it was used in the print function. Despite rechecking the logic, the error persisted.

I decided to use Valgrind after reading a Stack Overflow post on memory corruption detection (link: [StackOverflow](https://stackoverflow.com/questions/5134891/how-do-i-use-valgrind-to-find-memory-leaks)), which helped identify an invalid read (`valgrind-out.txt` from commit @fe77c478). I also used GDB to step through each line of code. Finally, it occurred to me that if a block was split and the remaining free block was too small to store a footer, it could corrupt adjacent memory regions. To resolve this, I modified the split logic to ensure the remaining free block could accommodate a footer. Ultimately, I ended up using footers for both free and allocated blocks, which resolved the issue.

**Known Bugs**
Rather than a bug, this issue can be considered more of an implementation limitation. Initially, I planned to set footers only for free blocks, leveraging the second least significant bit of the `size_and_flags` field in the Block struct. This bit would indicate the allocation status of the previous block: if set, the previous block was allocated and would have no footer; if unset, the previous block was free and would have a footer. This approach ensured we would only read valid footers and avoid interpreting junk data when footers didn't exist.

However, due to time constraints and complexity in refining this mechanism, I decided to temporarily skip its implementation, leaving it as an area for future enhancement. The current design, therefore, doesn't fully incorporate this optimization.

## Benchmarking
