# Lecture 198 - userspace sleep (parts 1 + 2 squashed)

Upstream: PeachOS64BitCourse 044b909 + 7607a68.

Two upstream commits both numbered L198. Part 2 fixes a typo
that part 1 inherited from L197, so SamOs squashes them to land
a clean state in a single commit.

## What landed

- `SYSTEM_COMMAND25_UDELAY` in the syscall enum.
- `isr80h/time.h` + `time.c`: `isr80h_command25_udelay` pops a
  microsecond count off the task stack, calls `task_sleep`, and
  enters `task_next` (which never returns into the syscall).
- Registration in `isr80h_register_commands`.
- Makefile FILES + recipe for `isr80h/time.o`.

Part 2 fixes the L197 deadline bug:
`tsc_microseconds() * microseconds` becomes
`tsc_microseconds() + microseconds`.

The userland stdlib `delay.{h,c}` and the `blank.c` test loop
are not ported; SamOs userland stays at its prior batch.

## BIOS test status

Source + link. Build links.
