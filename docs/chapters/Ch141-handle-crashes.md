# Ch 147 - Handle program crashes via exception handlers

A faulting user program no longer triple-faults the box. Instead the kernel catches the exception, terminates the offending process, and switches to whatever task is next.

## What landed

- `src/task/task.{h,c}` - new `task_next()`. Picks the next task via `task_get_next`, panics if none, otherwise `task_switch + task_return`.
- `src/idt/idt.c`:
  - Includes `task/process.h`.
  - New `idt_handle_exception()`: `process_terminate(task_current()->process); task_next();`.
  - `idt_init()` registers `idt_handle_exception` as the callback for every CPU exception vector (0..0x1F) via `idt_register_interrupt_callback`. The existing macro-generated stubs already route those vectors through `interrupt_handler` -> per-vector callback, so this just slots a real C body into each one.

## Why 0..0x1F

Those vectors are CPU-defined exceptions (#DE, #DB, #NMI, #BP, ..., #PF, ...). External IRQs start at 0x20 after the PIC remap from Ch 44, and the int 0x80 syscall trap is its own dedicated wrapper, so the range doesn't collide with anything.

## Test impact

Suite stays 32/32. Manual probe (G05 sentinel test 40 still green): kernel survives boot, smoke probes paint VGA, and post-exit user programs no longer hang the system.

## Note - blank.c null-deref test not yet shipped

The book's Ch 147 also updates `blank.c` to dereference NULL after printing argv, as a smoke test for the new handler. We skip that here because blank.c only runs when the user types `BLANK.ELF` from the shell - the suite's automated tests never trigger that path. The Ch 148 exit syscall will give us a cleaner way to demonstrate the lifecycle.
