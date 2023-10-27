# Lecture 154 - userspace windows (parts 1 + 2 squashed)

Upstream: PeachOS64BitCourse 0747b60 + 1e7fef0.

Two upstream commits land together here:

- Part 1 (0747b60) adds the process-side window plumbing.
- Part 2 (1e7fef0) wires the `isr80h_command16_window_create`
  syscall and the Makefile entries.

We squash them so the kernel still builds after a single commit.

## What landed

`struct process_window` pairs a userspace handle with the kernel
window. `struct process_userspace_window` is the userspace
mirror (title + width + height), allocated through
`process_malloc` so it lives in the process's address space.

`process` gains a `windows` vector and five helpers:

- `process_window_create` centres a kernel window on the screen,
  allocates the userspace mirror, and returns the kernel record.
- `process_owns_kernel_window` is a linear scan used by the
  event-delivery side.
- `process_get_from_kernel_window` walks the global process
  vector to find the owner of a given kernel window.
- `process_window_get_from_user_window` resolves a userspace
  handle back to the kernel record.
- `process_close_windows` calls `window_close` on every owned
  window. We call it at the top of `process_terminate` so the
  kernel windows come down before the per-process allocations
  do, since the kernel buffers live in kheap.

`isr80h/window.c` lands `isr80h_command16_window_create`. It
pulls title/width/height/flags/id off the task stack and hands
back `proc_win->user_win` as the userspace handle. The TODO
about "free the window" is kept verbatim because the upstream
implementation is also incomplete here.

`window_close` gets a header decl that the windowing module
already implemented (L138 wrote the body).

## BIOS test status

Source + link. Build links.
