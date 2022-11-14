# G04 - iret to ring 3 appears to triple-fault [FIXED]

## Symptom

Ch 98 added `process_load("0:/blank.bin") + task_run_first_ever_task()`
to `kernel_main`. The kernel reached the launch point (`entering
userland...` showed on the VGA buffer), no panic fired, but QEMU
monitor's `info registers` reported `CS=0x0008` and `EIP` somewhere
outside the kernel image (e.g. `0x10002c`, sometimes BIOS shadow at
`0xef39a` after a reset). The expected ring-3 state - `CS=0x001B`,
`EIP=0x00400000`, `CPL=3` - never materialised.

## Root cause

`task_init` was missing the line that sets the task's saved
**code segment register**. The book's Ch 98 explicitly says:

> // ADD THIS
> task->registers.cs = USER_CODE_SEGMENT;

That assignment somehow didn't land in `src/task/task.c` (the
ss/esp/ip lines did, but cs was left at zero from the `memset`).
Consequence: when `task_return` ran, it pushed `[ebx+32]` =
`registers.cs` = `0` onto the synthetic interrupt frame.

`iretd` then popped `CS = 0`, which is the NULL selector. That's an
immediate #GP. The kernel's `no_interrupt` handler iret'd back to
`task_return`'s iretd, which #GP'd again. Eventually a stack
overflow / nested-exception cascade triple-faulted the CPU and
QEMU reset. After reset, the bootloader ran again, kernel loaded,
crashed, repeat.

The trace showed the CPU at `EIP=0x10002c` (kernel.asm `jmp $`)
because between resets we'd sometimes catch a fresh kernel that had
just returned from `kernel_main` and was spinning, but with the
launch live the kernel never reaches that line on the first boot -
the second `EIP=0xef39a` etc. observations confirmed CR0.PG=0
(post-reset, before the new boot enabled paging again).

## Fix

One line in `src/task/task.c::task_init`:

```c
task->registers.cs = USER_CODE_SEGMENT;
```

added right next to the existing `task->registers.ss` /
`registers.esp` / `registers.ip` assignments.

After the fix, QEMU monitor reports exactly the expected post-iret
state:

```
EIP=00400000 EFL=00000216 [----AP-] CPL=3
CS =001b 00000000 ffffffff 00cff800 DPL=3 CS32 [---]
CR0=80000011 CR2=00000000 CR3=01XXX000 CR4=00000000
```

CPL=3, CS=0x1B (user code | RPL=3), EIP=0x00400000 (blank.bin's
`jmp $`), paging on, CR3 pointing at the task's page directory.

## Tests retasked

- `tests/37-ring3-reached.sh` flipped from asserting the
  deferred-launch marker to asserting CPL=3, CS=0x001B,
  EIP=0040000x via QEMU monitor.
- `tests/05-enters-protected-mode.sh`, `tests/08-kernel-loaded.sh`,
  `tests/09-kernel-main-runs.sh` now accept either kernel-mode
  state (CS=0x08, EIP in kernel.bin range) *or* ring-3 user state
  (CS=0x1B, EIP=0x400000). They asserted only the kernel state
  before; with the launch live the kernel hands off to ring 3
  before the 10-12 s sample fires.

## Things we tried that did not help (left in the historical record below)

- Adding `PAGING_IS_WRITEABLE` to the task page directory.
- Changing the user code GDT type from `0xF8` to `0xFA`.
- Adding `__attribute__((packed))` to `struct registers`.

All three were ruled out by the same wedge: `iretd` was popping a
NULL CS regardless of writability, readability, or struct padding.
Only setting `registers.cs` to a valid user code selector mattered.

## Lesson

Trust QEMU's `info registers` more aggressively. The diagnostic
that finally cracked this was a `grep "registers.cs"` on
`src/task/task.c` showing no match - meaning the field literally
wasn't being initialised. We'd been chasing iret semantics for
half an hour while the fix was a single missing C statement.
