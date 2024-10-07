---
mark: 70

section_marks:
  code: 42
  report: 28
---

## Code
- sorry for the stress with marking, it was an admin issue
- I'm not certain the 32 bit offset would work in all cases, given virtual memory can pick from practically any address which might not be within 2^32 bits. However, it's still a creative idea so will reward it!
- However the choice to remove references to prev. While it does still work, it means your "remove_from_free_list" is now O(n) time where n is the size of the free list, it works but it does make the results less efficient s
- Calls get_chunk_from_os but never checks if it fails so mmap tests fail
- GC sometimes frees random addresses (like 0x10) and very often frees blocks that are still in scope
- I think its because you have a check that marks inner blocks if a function returns NULL?
- Changing this makes the marking work, but it will still often free the same block multiple times or 0x10 still. So there's problems with sweeping
- so can only have 256 chunks, not unreasonable but a limitation
- why not include the chunk size in the chunk struct rather than using a seperate array
- it'd be a good idea to add in list_fit a case for if the diff = 0 because obviously you can't find a smaller block then

## Report
- One thing not explained in your report is how next_offset can change. Pretty unclear since its used in multiple contexts by the comments
- Decent discussion of testing and optimisations, not much to say
- benchmarking results like that are possible, there's more setup, for example, to multiple free lists before they become more efficient



