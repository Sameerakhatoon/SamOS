# Ch 131 - free syscall (cmd 5) + stdlib free()

The mirror of Ch 130. `free()` now actually releases memory through a new SYSTEM_COMMAND5_FREE syscall. The kernel validates that the freed pointer was originally allocated by THIS process before honoring the request, which doubles as a security guard - a misbehaving user can't free arbitrary kernel pointers or another process's allocations.

## What landed

Kernel side:
- `src/task/process.c`:
  - `process_is_process_pointer(process, ptr)` - linear scan of `allocations[]`; true iff `ptr` is one of ours.
  - `process_allocation_unjoin(process, ptr)` - zeros the slot, restoring "free" state.
  - `process_free(process, ptr)` - if `process_is_process_pointer` says no, return silently. Otherwise unjoin then `kfree`.
- `src/task/process.h` - exports `process_free`, includes `<stdbool.h>`.
- `src/isr80h/isr80h.h` - new `SYSTEM_COMMAND5_FREE` enum value.
- `src/isr80h/heap.{c,h}` - new `isr80h_command5_free` pulls `ptr` off the user stack and calls `process_free`.
- `src/isr80h/isr80h.c` - registers cmd 5.

User-space side:
- `programs/stdlib/src/samos.asm` - new `samos_free(ptr)` routine wrapping `int 0x80` with `eax = 5`.
- `programs/stdlib/src/samos.h` - prototype.
- `programs/stdlib/src/stdlib.c` - `free()` body now calls `samos_free(ptr)` (was empty).
- `programs/blank/blank.c` - calls `free(ptr)` right after `malloc(512)`.

## Caveat (deferred)

`process_free` doesn't unmap pages from the user's page directory yet. Same reason `process_malloc` doesn't map them: paging-aware versions of both land later. For now this is purely heap bookkeeping.

## Test impact

None. All 32 tests pass. blank.c's `malloc(512); free(ptr);` round-trip succeeds silently.
