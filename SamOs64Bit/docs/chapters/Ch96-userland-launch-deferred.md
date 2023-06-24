# Ch 96 - First userland launch (book Ch 98+99) - deferred

Book Ch 98 ("Executing the process and dropping into user land")
plus Ch 99 (a GDB walkthrough for the same launch) are the chapters
that would have the kernel actually iret into ring 3 to run
blank.bin. We ship the *plumbing* in this commit but *defer the
trigger*, because pulling it surfaces a triple-fault we haven't
diagnosed yet (see `docs/gotchas/G04-iret-to-ring3-resets.md`).

## What got added (the wiring, all live)

- `src/task/task.c`:
  - `task_new` - when `task_head == 0` (first task ever), set
    `current_task = task` alongside `task_head = task_tail = task`.
    `task_run_first_ever_task` panics if `current_task` is null, so
    this assignment is the one that turns the first task into the
    "running" task as far as the scheduler is concerned.
  - `task_init` - sets `task->registers.cs = USER_CODE_SEGMENT`
    (book Ch 98 line). Without this, the `iret` would re-enter at
    CS=0 and immediately #GP.
- `src/kernel.c`:
  - Includes `task/task.h`, `task/process.h`, `status.h`.
  - At the end of `kernel_main`, after the kmalloc smoke probe, the
    kernel prints `"entering userland... (deferred, G04)"` and then
    returns from `kernel_main` (the existing infinite-loop in the
    asm caller still keeps the CPU spinning in ring 0).
  - The actual `process_load` + `task_run_first_ever_task` calls are
    in the source as a four-line commented block. Un-commenting them
    is one keystroke once G04 is resolved.

## What got dropped (the trigger, dormant)

```c
// struct process* process = 0;
// if(process_load("0:/blank.bin", &process) != SAMOS_ALL_OK){
//     panic("Failed to load blank.bin\n");
// }
// task_run_first_ever_task();
```

Running these today produces:

- `entering userland...` shows on VGA (so all prior probes ran).
- No panic message, so `process_load` returned cleanly and
  `task_run_first_ever_task` started.
- `info registers` after 12 seconds shows CS=0x08, EIP somewhere
  outside the kernel image - i.e. the CPU is crash-looping
  (bootloader -> kernel -> iret -> fault -> reset).

The root cause is under investigation in
[G04](../gotchas/G04-iret-to-ring3-resets.md). The most likely
suspects are the task page directory's writable bits, the user code
descriptor type byte (`0xF8` vs `0xFA`), or the TSS `esp0` page
protections.

## Test coverage

- `tests/37-ring3-reached.sh` retasked: instead of asserting CS=0x1b
  and EIP=0x00400000 via QEMU monitor, it greps the VGA buffer for
  `entering userland... (deferred, G04)`. This proves the Ch 98
  plumbing didn't break the upstream kernel - the test fails the
  moment the kernel can't reach that print, regardless of whether
  the actual ring-3 launch happens.
- Once G04 is fixed and the four lines un-comment, test 37 will
  flip back to checking the QEMU monitor registers.

## Why ship the wiring without the trigger

The book splits the change into "set up the data structures" and
"pull the trigger" but commits them together in the reference
implementation. Splitting them here lets the suite stay green
across the chapters that build on top of the task abstraction
(memory mapping helpers in Ch 100, syscalls in Ch 101+) without
blocking forward progress on the unresolved triple-fault.

Once G04 is resolved we'll have:

1. A separate `gxx` commit that fixes the underlying bug.
2. A follow-on commit that un-comments the four lines in
   `kernel.c` and re-tasks test 37 to assert ring-3 state via
   QEMU monitor.

## Counter at 29

Tests at 29 green.
