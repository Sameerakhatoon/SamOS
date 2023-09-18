# Lecture 115 - userland realloc + calloc

L115 plugs `realloc` into the userland C API on top of the L80
`krealloc` and wires `calloc` while the registers are open.

## Kernel-side

- `SYSTEM_COMMAND15_REALLOC`.
- `isr80h_command15_realloc` (in `isr80h/heap.c`) reads
  `old_ptr` (stack item 0) and `new_size` (item 1), then
  forwards to `process_realloc`.
- `process_allocation_exists(process, ptr, &index)` is a new
  helper that walks the per-process allocations vector for the
  matching `ptr` and reports the index. Returns `-ENOTFOUND`
  on miss.
- `process_realloc(process, old_virt_ptr, new_size)`:
  1. Look up the existing allocation index.
  2. Translate the userland virtual pointer through
     `task_virtual_address_to_physical`.
  3. Call `krealloc` (L80) to grow / shrink / move the
     physical region.
  4. `process_allocation_set_map` overwrites the slot with the
     new ptr / end / size and re-maps the pages.

Userland gets back the same userland virtual address it had
when the allocation slot's identity-map already covered the
new region; if `krealloc` moved the block, it gets the new
virtual address.

## Upstream bug preserved

`process_realloc` writes

```c
old_phys_ptr = old_virt_ptr;
if(old_phys_ptr){
    old_phys_ptr = task_virtual_address_to_physical(process->task, old_virt_ptr);
    ...
}
```

The first assignment is dead - the second line clobbers it
unconditionally. The guard `if(old_phys_ptr)` is therefore
`if(old_virt_ptr != NULL)` in practice; it would have been the
same had the guard been written that way directly. Carried
verbatim for diff hygiene.

## Userland-side

- `samos.asm::samos_realloc` trampoline: rdi = old_ptr, rsi =
  new_size, rax = 15, int 0x80.
- `samos.h` declares the trampoline.
- `stdlib/stdlib.h` declares `realloc` and `calloc`.
- `stdlib/stdlib.c`:
  - `realloc(ptr, new_size)` is a one-liner forwarder.
  - `calloc(n_memb, size)` is `malloc` + `memset(0)`. Pulls in
    `memory.h` for `memset`.

## New errno

`status.h` gets `ENOTFOUND = 11`, used by
`process_allocation_exists` and any future module that wants a
generic miss code.

## BIOS test status

Source-only. Test verifies every kernel + userland symbol, the
trampoline + wrappers, the calloc memset, and that the build
links.
