# Ch 97 - Paging API refactor (book Ch 100)

Book Ch 100: rewrite the public paging functions so they take a
`struct paging_4gb_chunk*` instead of a raw `uint32_t*` directory
pointer. The internal `paging_set` and the asm `paging_load_directory`
keep their raw `uint32_t*` signature because they really do operate
on the directory bytes; everything above that gets the typed wrapper.

## What got refactored

### `src/memory/paging/paging.h` prototypes

```c
void paging_switch(struct paging_4gb_chunk* directory);
int  paging_map_to(struct paging_4gb_chunk* directory, void* virt,
                   void* phys, void* phys_end, int flags);
int  paging_map_range(struct paging_4gb_chunk* directory, void* virt,
                      void* phys, int count, int flags);
int  paging_map(struct paging_4gb_chunk* directory, void* virt,
                void* phys, int flags);
```

`paging_set`, `paging_align_address`, `paging_is_aligned`,
`paging_new_4gb`, `paging_free_4gb`, `paging_4gb_chunk_get_directory`
all unchanged.

### `src/memory/paging/paging.c` bodies

- `paging_switch` now unwraps the chunk: passes
  `directory->directory_entry` to the asm `paging_load_directory`
  and caches the same pointer in `current_directory`.
- `paging_map` unwraps to call `paging_set(directory->directory_entry,
  ...)`.
- `paging_map_range` and `paging_map_to` carry the chunk through; the
  range loop calls `paging_map` per page, and `_to` delegates to
  `_range`.

### Call sites updated

- `src/kernel.c` - `paging_switch(kernel_chunk);` (was
  `paging_switch(paging_4gb_chunk_get_directory(kernel_chunk));`).
- `src/task/task.c` - `paging_switch(task->page_directory);` (was
  `task->page_directory->directory_entry`).
- `src/task/process.c::process_map_binary` -
  `paging_map_to(process->task->page_directory, ...);` (was
  `->directory_entry`).

## Why this matters

We had three pieces of code reaching through the chunk to its raw
`uint32_t*` field. Each was an opportunity to mix up which directory
we meant - and we'd already had `task_switch` working off
`task->page_directory->directory_entry` while everywhere else used
the chunk pointer. Surfacing the chunk type at every call boundary
removes the temptation.

Mechanically nothing changes - the same `uint32_t*` lands in `cr3`
when paging_switch runs. The refactor is purely API hygiene before
the syscall arc starts adding more callers.

## Test coverage

All 29 existing tests pass unchanged, including the ring-3 entry in
test 37 (kernel still drops to CPL=3 at `0x400000` after the
refactor). No new test is added because the refactor doesn't change
observable behaviour.
