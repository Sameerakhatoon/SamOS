# Ch 141 - Unmap memory on free + struct process_allocation

`process_free` now unmaps the freed pages from the user's page directory. Without this, a future allocation that recycles the same physical address would still be reachable through the old process's (now stale) page-table entries - a security and correctness hole.

To know HOW MANY pages to unmap, we need to remember the size of each allocation, not just its address. So `allocations[]` becomes an array of `struct process_allocation { void* ptr; size_t size; }` instead of plain `void*`.

## What landed

- `src/task/process.h`:
  - New `struct process_allocation { void* ptr; size_t size; }`.
  - `process->allocations[]` typed as `struct process_allocation[SAMOS_MAX_PROGRAM_ALLOCATIONS]`.
- `src/task/process.c`:
  - `process_find_free_allocation_index` - compares `.ptr == 0` to find empty slots.
  - `process_get_allocation_by_addr(process, addr)` - new helper; linear scan returning the matching slot or NULL.
  - `process_malloc` - on success now writes `.ptr` AND `.size`.
  - `process_is_process_pointer` - checks `.ptr`.
  - `process_allocation_unjoin` - zeros both `.ptr` and `.size`.
  - `process_free` rewritten: lookup via `process_get_allocation_by_addr`, then `paging_map_to(... allocation->ptr, allocation->ptr, paging_align_address(allocation->ptr + allocation->size), 0x00)` - flags=0 strips PRESENT/WRITEABLE/USER so the user task can no longer touch those pages; then unjoin + kfree.
- `programs/blank/blank.c` - demonstrates the trap:
  ```c
  char* ptr = malloc(20);
  strcpy(ptr, "hello world");
  print(ptr);
  free(ptr);
  ptr[0] = 'B';        // page-faults - pages are unmapped
  print("abc\n");      // never reached
  ```
  Book guidance: if you DO see "abc" the unmap didn't take. If you don't, the unmap worked and the user triple-faulted on the post-free write (because we still don't have a page-fault handler installed).

## Test impact

None visible in the suite. 32/32 stays. The triple-fault from the post-free write happens AFTER all boot smoke probes paint VGA, and tests 37/38/39/40 don't depend on the blank user program reaching a particular state.
