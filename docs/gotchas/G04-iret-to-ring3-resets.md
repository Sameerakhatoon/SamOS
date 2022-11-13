# G04 - iret to ring 3 appears to triple-fault

## Symptom

Ch 98 adds `process_load("0:/blank.bin") + task_run_first_ever_task()`
to `kernel_main`. The kernel reaches the launch point (`entering
userland...` shows on the VGA buffer), no panic fires, then... QEMU
monitor's `info registers` 12 seconds later reports `CS=0x0008` and
`EIP` somewhere outside the kernel image (e.g. `0x001cf4bd`).

Expected once `task_return`'s `iretd` succeeds:
- `CS = 0x001B` (user code selector | RPL=3)
- `EIP = 0x00400000` (blank.bin's `jmp $`)

Actual: kernel-mode CS, garbage EIP. The CPU appears to be
crash-looping (bootloader -> kernel boot -> iret -> fault -> reset).

## What's known to work

- `process_load` runs to completion (no panic, `entering userland...`
  is the last text printed).
- The synthetic interrupt frame layout matches the book.
- The Ch 95 task.asm code linked correctly (test 36).
- Test 37 (this test) just checks the launch point is reached.

## What's likely broken

Suspects we need to investigate when we come back to this:

1. **CR0.WP and writable PTEs.** The task page directory is allocated
   with `paging_new_4gb(PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL)`
   - no `PAGING_IS_WRITEABLE`. CR0.WP defaults to 0 so ring 0 ignores
   write bits, but the ring-3 user stack at `0x3FF000` would page
   fault on any push. blank.bin doesn't push... but an incoming IRQ
   would force a stack switch via TSS `esp0` (`0x600000`) which is
   also non-writable in the task PD. If IF=1 in the iret EFLAGS and
   PIT IRQ0 is unmasked, the very first timer tick after `iretd`
   double-faults trying to push the user state.
2. **GDT user code descriptor type byte.** Book uses `0xF8` which is
   present + DPL=3 + S=1 + code, non-conforming, *non-readable*. Most
   tutorials use `0xFA` (readable code). x86 doesn't require code
   readability for fetch, but if any segment register load uses CS as
   a source it'd #GP.
3. **TSS `esp0` page protection.** We point esp0 at `0x600000` but
   never explicitly map the kernel stack range with writable PTEs in
   the task's address space.
4. **`restore_general_purpose_registers` interaction with EBP**. The
   Ch 97 fix patched the trailing `pop esp` to `pop ebp`, but the
   call site immediately precedes `iretd` - if anything stomps EBP
   between them the `iretd` could iret with the wrong frame.

The book itself has a debugging chapter (Ch 99) that recommends GDB
to inspect this point - which suggests the author may have had to
debug something similar during development.

## Workaround for now

Test 37 just asserts the launch point is reached (i.e. that Ch 98
plumbing is wired without panicking). The full "CPU is in ring 3"
assertion is deferred until we resolve the root cause.

## Debugging session 1 findings

A first pass with QEMU `-d int,cpu_reset` and `info registers`
inside the monitor produced:

- After enabling the launch, the CPU ends at `CPL=0`,
  `EIP=0x10002c` which is exactly the `jmp $` at the end of
  `kernel.asm` (confirmed via `objdump -D bin/kernel.bin`). The
  kernel is spinning post-`kernel_main`.
- `CR0=0x80000011` (PE+MP+PG enabled), `CR3=0x01004000` (task's
  page directory) - so the `paging_switch` to the task's PD did
  work. We're stuck post-`task_return` though, not in user code.
- After 12 seconds of QEMU run, `info registers` sometimes catches
  CR0.PG=0 with CS=0x08 EIP somewhere in the 0xef000 BIOS shadow
  range. That signature is the bootloader running again - the CPU
  has triple-faulted, reset, and is rebooting. The "kernel returns
  to jmp $" version above is what we see when we catch it during a
  successful boot cycle.
- The QEMU int trace shows: vector 0x20 (PIT IRQ0) fires while
  kernel is at `0x10072a` (somewhere inside `kernel_main`), then
  a v=0x76 event (still unidentified - possibly an SMI or QEMU
  internal), then v=0x20 repeats at `0x10002c` (the `jmp $`).
  The 0x20 firing is *before* `paging_switch` and `task_return`;
  the `no_interrupt` handler at IDT[0x20] handles it cleanly. The
  surprise is the kernel reaching `0x10002c` (i.e. `kernel_main`
  returning) at all - if `task_return` ever transitioned to ring
  3, the `jmp $` infinite loop in blank.bin would prevent return.

So `task_return`'s `iretd` is *not* taking us to ring 3.

## Things we tried that did not help

- Adding `PAGING_IS_WRITEABLE` to the task page directory creation
  flags (`paging_new_4gb`). Reasoned this would allow the TSS
  `esp0` stack to be written. No effect.
- Changing the user code GDT type from `0xF8` (execute-only) to
  `0xFA` (readable code). Modern x86 lets you fetch from
  execute-only segments; this is more conservative. No effect.

## Things to try next

- Verify the struct registers offsets match what task.asm assumes.
  Possible: GCC could pad/align the struct in a way that shifts
  `cs`/`ip`/`ss`/`esp` away from where task.asm reads them. Quick
  check: add `__attribute__((packed))` to `struct registers` and
  see if behavior changes.
- Trace the exact stack contents right before `iretd`. Add an
  `int3` instruction two instructions before `iretd` and run with
  QEMU monitor's `info registers` to peek at `ESP+0..16`.
- Try a simpler `iretd` test: hardcode `cs=0x08`, `ip=0x10002c`,
  `ss=0x10`, `esp=0x200000` in a temporary copy of task_return,
  so we iret to *kernel* mode at the kernel `jmp $`. This isolates
  whether `iretd` works at all vs. whether the privilege transition
  is the problem. Successful iret to same ring proves the rest of
  the setup is sane; failure means there's a more fundamental
  issue.
- Inspect the kernel image's `.asm` section is being linked at the
  expected base address. The `R` (rodata) tag on task_return in
  `nm` output is suspicious - though execution does work for the
  other asm functions, so probably not the issue.
