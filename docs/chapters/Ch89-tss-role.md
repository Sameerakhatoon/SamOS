# Ch 89 - Role of the TSS in our kernel (prose)

Book Ch 87. Prose follow-up to Ch 88's "what a TSS is". No code.

## The single concrete thing the TSS does for us

Provide `esp0` (and `ss0`) when an interrupt or syscall fires while
user code is running. The CPU:

1. Already knows the interrupted process's CS/EIP/SS/ESP/EFLAGS
   (because those are the registers it's currently using).
2. Looks at the IDT gate, sees DPL=3 is allowed.
3. Reads the TR register, walks to the TSS, pulls out `esp0`+`ss0`.
4. *Swaps to that stack.*
5. Pushes the user-side registers from step 1 onto the (now kernel)
   stack as the interrupt frame.
6. Pushes the error code (for some interrupts) and the return EIP,
   then jumps to the handler.

If `esp0` was zero or pointed somewhere the CPU couldn't write,
step 4 -> step 5 would triple-fault the machine before the handler
ever ran.

## Why one TSS suffices for us

Modern kernels (Linux, BSD) have one TSS *per CPU core*, and they
rewrite that TSS's `esp0` on every context switch so the kernel
stack of the now-current process is always pointed at. We have
exactly one core to ourselves, so one global TSS works: write its
`esp0` once to a dedicated kernel stack area and forget about it.

When (if) we ever support multi-tasking later, we'll need to
swap `esp0` on every context switch so each user task picks up
*its* kernel stack on the next trap. Today, single task -> single
`esp0` -> done.

## What's next

- Ch 88 (book Ch 88): worked example of `int 0x80` from user code,
  step by step.
- Ch 89 (book Ch 89): rewrite the kernel's GDT in C, paving the way
  for adding the TSS descriptor + user code/data descriptors.
- Ch 90+: actually allocate the `struct tss`, fill `esp0`/`ss0`,
  add it as the fourth GDT entry, `ltr` the TR.
