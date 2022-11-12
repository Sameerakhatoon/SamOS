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

This g-commit isn't a code change yet; it's a placeholder so a
future debugging session can attack the right surface.
