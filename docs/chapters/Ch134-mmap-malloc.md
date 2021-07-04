# Ch 140 - Memory-map malloc'd pages into user PT

The missing piece of Ch 130. `process_malloc` now actually maps the kzalloc'd memory into the calling user process's page directory so the user can read AND write it. Without this map, the malloc'd address was kernel-only - touching it from ring 3 would page-fault.

## What landed

- `src/task/process.c::process_malloc` rewritten:
  - `kzalloc(size)` -> `ptr`. Bail to `out_err` on null.
  - `process_find_free_allocation_index`. Bail on `< 0`.
  - `paging_map_to(process->task->page_directory, ptr, ptr, paging_align_address(ptr + size), PAGING_IS_WRITEABLE | PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL)`. Identity-maps the physical address into the user's PT with W/U bits set. Bail on `< 0`.
  - Plant the pointer into `process->allocations[index]`, return it.
  - `out_err:` kfrees `ptr` if non-null, returns 0.
- `programs/blank/blank.c` - demonstrates the fix:
  ```c
  char* ptr = malloc(20);
  strcpy(ptr, "hello world");
  print(ptr);
  while(1){}
  ```

## Test impact

None. 32/32 stays. Boot the kernel, type `BLANK.ELF` in the shell, hit Enter, and you see "hello world" printed - which used to crash before this map.
