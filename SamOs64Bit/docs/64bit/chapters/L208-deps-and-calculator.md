# Lecture 208 - userspace deps + calculator

Upstream: PeachOS64BitCourse 23314f8.

## What landed

The upstream commit is 116 files / 6.7K lines. It introduces:

- `programs/calculator/`: a complete calculator GUI.
- `programs/containerlib/`: a userland vector implementation.
- `programs/guilib/`: GUI widget library backing the calculator.
- `programs/binary_test/`: smoke test program.
- An overhauled `programs/stdlib/`: status codes, fcntl-style
  modes, peachos C helpers, expanded stdio/stdlib/string,
  memory.{c,h}, delay.{c,h}, start.{c,asm}.
- `programs/blank/blank.c` rewrite that exercises every L162-
  L173 userland window syscall.
- README at `programs/README.md` documenting the userland.

## SamOs kernel-side delta

Two changes:

- `kernel.c` switches the default `process_load_switch` arg
  from `blank.elf` to `shell.elf`. SamOs's caller is dead code
  (the L? spin loop above blocks) but the rename matches
  upstream parity.
- The Makefile additions for calculator / containerlib /
  guilib are NOT ported: SamOs's program tree does not track
  upstream's userland in this branch. A future userland-port
  pass will revisit.

## Module 2 port complete

L105-L208 inclusive (skipping the few upstream lectures with
no kernel-side delta) are now committed on the `module2-64bit`
branch. The build is green; the audit trail records every
upstream typo or bug preserved with a sic marker.

## BIOS test status

Source + link. Build links.
