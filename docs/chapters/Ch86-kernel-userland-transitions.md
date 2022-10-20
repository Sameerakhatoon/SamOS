# Ch 86 - Transitioning between kernel and user land (prose)

Book Ch 84. Still prose. No code yet.

## The transitions

Three flavours, all routed through interrupts:

1. **System calls (user -> kernel).** Ring 3 fires `int N` with N
   matching a syscall number. The CPU consults the IDT, finds the
   gate has DPL=3 (so user code is allowed to invoke it), switches
   to the kernel stack pointer cached in the TSS, pushes the
   pre-trap state, and jumps into the kernel handler.
2. **Hardware interrupts.** A device raises an IRQ; if user code is
   running, the CPU does the same TSS stack switch and dispatches
   the handler. Just like above except the source is hardware not
   software.
3. **Returning to user (kernel -> user).** Once the handler is done,
   `iret` pops the saved CS/EIP/EFLAGS/SS/ESP off the kernel stack
   and the CPU is back where user code was, in ring 3.

The "fake interrupt return" trick: to launch a user process for the
*first* time, the kernel manually constructs an interrupt frame on
the kernel stack (user CS, user DS, user ESP, EFLAGS with IF set,
user EIP = process entry point), then issues `iret`. The CPU has no
idea it wasn't really servicing an interrupt; it pops the frame and
finds itself executing user code at ring 3.

## Why our kernel never maps itself into user pages

The book commits to *not* mapping the kernel into every process's
address space (modern kernels do, with US=0 on the kernel pages
so user code can't read them). Our choice means a user process
literally cannot reach kernel memory; the only way to invoke kernel
code is via the syscall interrupt, which switches address spaces as
part of its TSS-driven stack swap.

## What's next

- Ch 87+: build the TSS struct and load it.
- Ch 90+: lay out a process control block (PCB) so the kernel can
  remember each task's pages, descriptors, open files.
- Ch 95+: implement `task_run_first_ever_task` (or whatever the
  book names it) - the `iret` jump to user code.

No code in this commit. Just framing for the implementation chapters
ahead.
