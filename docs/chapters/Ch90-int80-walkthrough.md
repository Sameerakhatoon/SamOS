# Ch 90 - int 0x80 walkthrough (prose)

Book Ch 88. Step-by-step trace of what the CPU does when ring-3
code executes `int 0x80`. No code change.

## The sequence

User code in ring 3 executes:

```asm
int 0x80
```

CPU steps (all automatic, none of this is kernel code):

1. **TSS lookup.** CPU reads the TR register, walks to the active
   TSS, extracts `esp0` and `ss0`.
2. **Stack swap.** ESP <- `esp0`, SS <- `ss0`. The processor is now
   pointed at the kernel stack, but still executing in ring 3 for
   one more cycle.
3. **Push the user frame.** CPU pushes (in order): user SS, user
   ESP (the pre-trap value), EFLAGS, user CS, user EIP (the address
   of the *next* instruction after `int 0x80`). If the interrupt
   has an error code (page fault, GP fault), it's pushed too. For
   `int 0x80` there's no error code.
4. **Ring switch.** CPU loads CS/DS/ES/FS/GS from the kernel
   descriptor selectors. CPL is now 0.
5. **Page-table swap.** *We* do this in software at the top of the
   handler. The CPU doesn't touch CR3 - if the kernel isn't mapped
   into the current process's address space, we need to load the
   kernel page directory before the first kernel-only memory
   access.
6. **Handler dispatch.** CPU reads the IDT entry at index 0x80,
   loads CS:EIP from the gate, starts executing the handler.

When the handler is done:

7. **Page-table swap back.** Load the user's CR3.
8. **`iret`.** CPU pops the saved EIP/CS/EFLAGS/ESP/SS. Ring 3
   resumes at the instruction right after `int 0x80`.

## Which steps are ours, which are the CPU's

- Hardware does: TSS lookup, stack swap, push user frame, ring
  switch, IDT dispatch, `iret` pop.
- Kernel does: install IDT entries with DPL=3 so user code is
  allowed to use `int 0x80`, fill the TSS, set CR3 swaps in/out of
  the handler, write the handler body itself.

## What changes for our kernel

Earlier `int` chapters (Ch 14-15 in book numbering, our Ch 16-17
docs) covered interrupts in real mode and protected mode while
running entirely in ring 0. Almost all of that still applies; what
this chapter adds is the *ring change* mid-flow, which is what
makes the TSS necessary.

We don't write code until the implementation chapters land. This is
the mental model we'll need when we do.
