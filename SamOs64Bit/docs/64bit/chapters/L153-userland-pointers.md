# Lecture 153 - userland pointers

Upstream: PeachOS64BitCourse 9401283.

The kernel must never hand a raw kernel address to userland.
L153 introduces a sentinel value, `struct userland_ptr`, that
userland holds onto in place of the real pointer. When userland
passes the sentinel back through a syscall, the kernel walks the
per-process registry to recover the actual `kernel_ptr`.

## What landed

- `src/task/userlandptr.h` declares `struct userland_ptr` and the
  four-call API.
- `src/task/userlandptr.c` implements:
  - `process_userland_pointer_create` allocates a sentinel and
    pushes it into the registry.
  - `process_userland_pointer_release` pops the sentinel and
    frees it.
  - `process_userland_pointer_registered` linear-scans for the
    sentinel.
  - `process_userland_pointer_kernel_ptr` re-resolves to the
    real kernel pointer.
- `struct process` gains `kernel_userland_ptrs_vector`, a
  `vector<struct userland_ptr*>`.
- `process_init` allocates the vector with initial capacity 4.
- The teardown path frees the vector after the allocations
  vector is freed.
- Makefile FILES gains `userlandptr.o` plus a compile recipe.

L154 is where the first consumer (window-create) starts handing
sentinels back through syscalls.

## BIOS test status

Source + link. Build links.
