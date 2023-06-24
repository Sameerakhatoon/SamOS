# Ch 88 - Understanding the Task State Segment (prose)

Book Ch 86. Prose explaining what the TSS is for. No code.

## The legacy and the modern roles

The TSS was designed for hardware multitasking: the CPU could do a
full context switch by just changing the Task Register to point at
a different TSS. Modern kernels (including the one we're building)
don't use that. We do software context switching - the kernel
explicitly saves/restores registers around task switches.

What we *do* use the TSS for is the ring-3 -> ring-0 stack switch.
When a user process is running and an interrupt or syscall fires,
the CPU:

1. Looks at the current privilege level (ring 3).
2. Consults the IDT gate's DPL to verify the call is allowed.
3. Reads the TSS pointed to by the Task Register, pulls `esp0` and
   `ss0` out of it.
4. Loads those into ESP/SS and pushes the pre-trap user state onto
   the new (kernel) stack.
5. Jumps into the handler.

Without a TSS, step 3 would fail and the CPU would triple-fault
trying to push user state to a stack it can't access.

## Fields we'll fill in

Of the ~26 fields in a 32-bit TSS, our kernel only needs:

- `esp0` - the kernel stack pointer for ring-0 entries.
- `ss0` - the kernel data selector (our GDT offset 0x10).
- A few zeroes everywhere else.

The rest (legacy hardware-task fields, I/O permission bitmap base)
stays zero; we never task-switch through the TSS itself.

## Where the TSS lives

Just like any other system descriptor, the TSS is referenced through
a GDT entry. The next code-bearing chapter:

1. Adds a TSS descriptor to the GDT (type byte = 0x89, granularity
   = 0).
2. Allocates a `struct tss` somewhere reachable (probably file-scope
   in `kernel.c`).
3. Writes a `load_tss` asm helper that does `ltr ax` after setting
   AX to the TSS GDT offset OR'd with the RPL.

Reading this chapter is enough to make sense of what those moves
actually mean.
