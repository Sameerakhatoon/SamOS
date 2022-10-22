# Ch 87 - Getting to userland for the first time (prose)

Book Ch 85. Prose; no code.

## The recipe

The book walks the bootstrap-into-ring-3 dance step by step:

1. **Set up user code + data segments in the GDT.** Two new
   descriptors with DPL=3. Base 0, limit 0xFFFFFFFF; user code is
   readable+executable, user data is readable+writable. Their
   selectors get the RPL=3 bit (`| 3`) so the CPU enforces ring 3
   for any far jump through them.
2. **Set up the TSS.** One TSS for the whole kernel (single core).
   The interesting field today is `esp0` - the kernel stack pointer
   the CPU snaps to whenever a ring-3 -> ring-0 trap fires.
3. **Lay out the process memory** - code segment, data segment,
   maybe stack, all in the process's own page directory so the
   user code can run from its expected virtual base (commonly
   `0x400000`).
4. **Fake an `iret` return:**
   - Push user SS (data selector | 3).
   - Push user ESP (top of the process's stack).
   - Push EFLAGS with IF set (so interrupts come back on once we're
     in ring 3).
   - Push user CS (code selector | 3).
   - Push user EIP (process entry point).
   - `iret`.

The CPU pops the frame, validates the privilege level transition,
loads CS/EIP at ring 3, and the user process is now running. No
"first instruction is special" path; it's the same `iret` we'd
hit when returning from any future interrupt.

## What we need before we can write this

- A `struct gdt_entry` C-side type so we can populate the new GDT
  entries from C instead of hand-assembling them. (Chapter 90+.)
- A `struct tss` definition + asm helper to load the TR register.
  (Chapter 87 next in book ordering, but it's the "understanding"
  rather than the implementation chapter.)
- A process control block (PCB) struct holding the code segment
  pointer, data pointer, page directory address, and EIP/ESP. The
  PCB is what the `iret` frame is built from.

All of those land as separate code commits in subsequent chapters.
This chapter is the "we agree on the recipe" prelude.
