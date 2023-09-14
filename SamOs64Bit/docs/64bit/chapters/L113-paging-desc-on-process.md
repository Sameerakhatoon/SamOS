# Lecture 113 - paging descriptor moves to the process

L113 reparents the paging descriptor: it was on `struct task`,
it now lives on `struct process`. Multiple tasks belonging to
the same process will share the descriptor (the L114 process
list refactor sets up the groundwork).

## Field move

`struct task` drops:

```c
struct paging_desc* paging_desc;
```

`struct process` gains:

```c
struct paging_desc* paging_desc;
```

`process.h` forward-declares `struct paging_desc` so callers
do not have to drag `paging.h` in.

## task.c forwards

- `task_switch(t)` calls `paging_switch(t->process->paging_desc)`.
- `task_paging_desc(t)` returns `t->process->paging_desc`.
- `task_virtual_address_to_physical(t, addr)` walks
  `t->process->paging_desc`.
- `task_init` no longer creates a descriptor or maps E820 - that
  work moves to `process_map_memory` and the process_load path.
- `task_free` no longer frees the descriptor.

## process.c

- Every `process->task->paging_desc` becomes `process->paging_desc`.
- `process_map_memory` opens with
  `paging_map_e820_memory_regions(process->paging_desc)`,
  taking over the responsibility task_init had.
- `process_load_for_slot` is reordered:
  1. Allocate the process; init it; load data; allocate stack.
  2. **NEW**: `paging_desc_new(PAGING_MAP_LEVEL_4)` into
     `process->paging_desc`.
  3. **NEW**: `process_map_memory(process)` - now safe because
     the descriptor exists.
  4. `task_new(process)`.
- `process_terminate` calls `paging_desc_free(process->paging_desc)`
  after `task_free` runs.

## Why the reorder

`task_new` was the place that built the descriptor and laid
down the e820 + program + stack mappings. With the descriptor
moved out, the same work has to land BEFORE `task_new` so the
task has something to point its register/state setup at.
Moving the calls into `process_load_for_slot` also keeps the
lifetime simple: one create on load, one free on terminate,
both inside the process module.

## BIOS test status

Source-only. Test verifies the field move, no stale
`task->paging_desc` references in task.c, no stale
`process->task->paging_desc` in process.c, the load-order
constraint (paging_desc_new before task_new),
paging_desc_free at teardown, the forward decl in process.h,
and that the build links.
