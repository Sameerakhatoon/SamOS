# Ch 94 - Processes (book Ch 92+93 combined)

The book splits the process layer across two chapters because the
first half (Ch 92) explicitly references functions added in the
second half (Ch 93). The chapter intro even warns: "the code won't
compile immediately after completing this section". Per project
convention (Ch 78+79, Ch 80+81), we ship the pair as one commit.

## What got added

### Constants

- `src/status.h` - `EISTKN = 8` (process slot is taken).
- `src/config.h`:
  - `SAMOS_MAX_PROGRAM_ALLOCATIONS = 1024` - per-process kmalloc
    quota the eventual process_malloc will enforce.
  - `SAMOS_MAX_PROCESSES = 12` - size of the global `processes[]`
    table.

### Paging extensions (`src/memory/paging/paging.{h,c}`)

- `paging_align_address(ptr)` - round up to the next 4 KiB boundary
  unless already aligned.
- `paging_map(directory, virt, phys, flags)` - map a single page,
  rejecting unaligned addresses with `-EINVARG`.
- `paging_map_range(directory, virt, phys, count, flags)` - call
  `paging_map` in a loop, advancing both pointers by 4 KiB per
  iteration.
- `paging_map_to(directory, virt, phys, phys_end, flags)` - the
  byte-range wrapper used by the process loader. Validates that
  all three addresses are page-aligned and that `phys_end >= phys`,
  then calls `paging_map_range` with `count = (phys_end - phys) /
  PAGE_SIZE`.

### String helper (`src/string/string.{h,c}`)

- `strncpy(dest, src, count)` - copy up to `count - 1` bytes from
  `src` into `dest`, stop early on null, always null-terminate.
  Used by `process_load_for_slot` to copy the filename.

### Task <-> process coupling (`src/task/task.{h,c}`)

- Forward decl `struct process;` in `task.h`.
- `struct task` gains a `struct process* process` field.
- `task_new` and (static) `task_init` now take a `struct process*`
  argument so every task knows its owning process.
- `task_init` writes `task->process = process` alongside the
  existing register seeding.

### Process layer (`src/task/process.{h,c}`)

- `struct process` (in `process.h`): id, filename (up to
  `SAMOS_MAX_PATH`), task pointer, allocations array, raw program
  pointer, stack pointer, size.
- File-scope state in `process.c`:
  - `struct process* current_process` - last-scheduled process.
  - `struct process* processes[SAMOS_MAX_PROCESSES]`.
- `process_init(process)` (static) - `memset` to zero.
- `process_current()` - one-liner accessor.
- `process_get(id)` - bounds-checked array lookup.
- `process_load_binary(filename, process)` (static) - open the file
  via VFS, `fstat` for the size, kzalloc that many bytes, fread the
  whole file into the buffer, stash `ptr`/`size`. `fclose` runs
  whether or not the read succeeded (book's pattern, even though it
  hits the goto-out before the open is checked).
- `process_load_data(filename, process)` (static) - thin wrapper;
  ELF support lands later and this is where the dispatch will live.
- `process_map_binary(process)` - call `paging_map_to` against the
  task's page directory to map virtual `0x400000..0x400000+size`
  (aligned up) onto the kzalloc'd `process->ptr`. Flags: present +
  access-from-all + writable.
- `process_map_memory(process)` - currently just calls
  `process_map_binary`; will grow to also map the stack.
- `process_get_free_slot()` - linear scan, returns first null slot
  or `-EISTKN`.
- `process_load_for_slot(filename, **process, slot)` - the main
  entry point. Reject if the slot's already taken, kzalloc a process,
  zero it, load the binary, kzalloc the user stack, copy in the
  filename, allocate a task with this process as owner, map the
  binary, write the result to `*process` and into `processes[slot]`.
- `process_load(filename, **process)` - convenience wrapper that
  calls `process_get_free_slot` then dispatches to
  `process_load_for_slot`.

### Makefile

- New `./build/task/process.o` in `FILES`, plus its build rule.

## What's intentionally left untested

- Nothing in `kernel_main` calls `process_load` yet. We have the
  layer but no caller. The "boot a process" flow lands once the
  user-space transition asm is in place (book Ch 95+).
- The error-path cleanup in `process_load_for_slot` only frees the
  task; it leaks `_process` and the stack on rollback. The book
  ships this bug; we'll g-fix it once we have a smoke test that
  exercises a failed load.

## Ch 94 - already in place

Book Ch 94 is a one-line "add `__attribute__((packed))` to
struct gdt". We shipped that in Ch 89 (because the GDT entries
wouldn't have matched their on-disk size without it), so Ch 94 is a
no-op for this repo. No separate commit; just a note here.

## Tests

All 27 existing tests still pass - the kernel still boots, opens
hello.txt, reads it, closes it. The process layer compiles cleanly
but is dormant until the userland transition lands.
