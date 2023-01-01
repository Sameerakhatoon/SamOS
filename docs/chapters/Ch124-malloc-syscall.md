# Ch 130 - malloc syscall (cmd 4) + stdlib malloc/free

User processes can now request memory from the kernel through a new SYSTEM_COMMAND4_MALLOC syscall. The kernel stashes the allocated pointer in the process's `allocations[]` array (set up way back in Ch 92) so it can track per-process memory.

## What landed

Kernel side:
- `src/isr80h/isr80h.h` - adds `SYSTEM_COMMAND4_MALLOC` to the enum (value 4).
- `src/isr80h/heap.{c,h}` - new translation unit. `isr80h_command4_malloc` pulls `size` off the user stack via `task_get_stack_item(task_current(), 0)` and calls `process_malloc(task_current()->process, size)`.
- `src/isr80h/isr80h.c` - registers cmd 4.
- `src/task/process.{c,h}`:
  - `process_find_free_allocation_index(process)` - linear scan over `allocations[]` returning the first NULL slot, or `-ENOMEM`.
  - `process_malloc(process, size)` - `kzalloc(size)`, then plant pointer into the free slot. Memory mapping into the user's page tables is NOT done yet (book defers it); the address returned is therefore kernel-visible only - the user process can't write to it without faulting. Book warns about this explicitly.
- `Makefile` - adds `build/isr80h/heap.o`.

User-space side:
- `programs/stdlib/src/samos.asm` - new `samos_malloc(size)` routine wrapping `int 0x80` with `eax = 4`.
- `programs/stdlib/src/samos.h` - prototype + `#include <stddef.h>` for `size_t`.
- `programs/stdlib/src/stdlib.{c,h}` - C standard library entry point. `malloc(size)` -> `samos_malloc(size)`. `free(ptr)` is a stub.
- `programs/stdlib/Makefile` - compiles `stdlib.c` -> `stdlib.o`, links it into `stdlib.elf`.
- `programs/blank/blank.c` - calls `malloc(512)` after the initial print.

## Naming deviation

The book names everything `peachos_*`. We use `samos_*`. Function semantics identical.

## Important caveat

Book: *"Do not try to write to the allocated memory in your user space process, the reason being is that we have not yet implemented memory mapping within the process_malloc function, this means the address returned is not writable from our user process at this time."* Ch 131+ will map the allocated memory into the user's page directory.

## Test impact

None. All 32 tests still pass - the blank user program returns a non-null pointer but doesn't dereference it, so the missing mapping doesn't matter yet.
