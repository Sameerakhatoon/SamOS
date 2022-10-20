# Ch 85 - Understanding user land (prose)

Book Ch 83. Pure prose preceding the userland implementation arc. No
source-tree changes.

## What user land actually means

x86 has four protection rings (0..3). The kernel runs in ring 0 -
unlimited access to ports, paging structures, privileged
instructions. Ring 3 is "user land": same CPU, deliberately fewer
capabilities. A ring-3 process that tries `in al, 0x60` or rewrites
CR3 gets a #GP exception that the kernel catches.

The points the chapter hammers:

- Privileged instruction attempts -> exception, handler decides what
  to do.
- Memory isolation is enforced by paging, not by ring alone. Each
  process has its own page directory that doesn't map kernel-only
  ranges; the ring level just controls whether the CPU honours the
  US bit.
- Virtual addresses are an illusion: two processes both think they
  own `0x400000` because their page directories map it to different
  physical pages.
- One process crashing doesn't take the kernel down; the worst it
  can do is fault and get its own descriptor cleaned up.

## Why we're building this

The book uses user land for: launching `shell.exe` and `blank.elf`,
demonstrating syscalls, multi-tasking. None of which work without
the ring-3 plumbing the next few chapters build:

1. New GDT entries for user code + data (DPL=3).
2. A TSS so the CPU knows which ring-0 stack to switch to on a
   ring-3 -> ring-0 trap.
3. An IRET trick that pushes a synthetic interrupt frame onto the
   stack and `iret`s into user code as if returning from an
   interrupt.

That's Ch 84 through Ch 90-ish in the book. This chapter is just
the conceptual primer.
