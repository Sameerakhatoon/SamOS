# Lecture 66 - blank.elf runnable in 64-bit + IDT struct bug fix

**Source commit (PeachOS64BitCourse):** `eaf51a5`
**SamOs commit:** L66 on `module1-64bit` branch
**Regression test:** `tests64/L66-blank-elf-loadable.sh`

## Why this chapter exists

L65 finished the ELF64 loader port. L66 has two pieces:

1. A struct-layout bug fix in `src/idt/idt.h`: `offset_3` was
   typed `uint16_t` (2 bytes) where it should be `uint32_t`
   (4 bytes). The wrong width would shrink the `idt_desc`
   struct by 2 bytes and shift `reserved`, breaking the
   IDT layout the CPU expects.

   SamOs already had this fixed in our L37 port (we wrote
   `uint32_t offset_3` from the start), so the L66 idt.h
   change is a no-op for us. Worth documenting as
   "previously-caught upstream bug".

2. `programs/blank/blank.c` reduced to a tight
   `print("Hello world\n")` loop. Same behaviour as upstream's
   L66 change.

## kernel.c load target deviation

Upstream flips `kernel_main` to load `0:/blank.elf` and run it
end-to-end. SamOs keeps SIMPLE.BIN as the regression-test
load target because:

- `blank.elf` is ~2 MiB. Even tightening the linker script's
  `ALIGN(4096)` to `ALIGN(16)` only saves a few KiB - the real
  bloat is `OUTPUT_FORMAT(elf64-x86-64)`'s default
  `p_align = 0x200000` which forces the PT_LOAD file offset
  to 2 MiB.
- ATA-PIO sector-at-a-time read in QEMU TCG takes ~40-60
  seconds for that much data. Our regression test budgets
  12 seconds, so flipping to BLANK.ELF would fail the test.

The L65 ELF64 loader DOES work end-to-end - we manually
verified that BLANK.ELF loads, validates, gets mapped, and
the user code at entry 0x401000 runs. We just don't put that
in the automated test.

L67+ probably brings IRQ-driven IO or DMA which would make
the load fast enough. SamOs will flip to BLANK.ELF then.

## Manual verification of the ELF run

```
PATH=$HOME/opt/cross/bin:$PATH
x86_64-elf-readelf -h programs/blank/blank.elf | head
```

shows:
- Class: ELF64
- Machine: AMD x86-64
- Type: EXEC
- Entry point: 0x401000

Boot kernel with `process_load_switch("0:/BLANK.ELF", &p)`,
wait 60+ seconds. The user code "Hello world" loop appears
on VGA.

## What the regression test asserts

`tests64/L66-blank-elf-loadable.sh`:
1. blank.elf is ELF64 / x86-64 / EXEC
2. Entry point >= SAMOS_PROGRAM_VIRTUAL_ADDRESS (0x400000)
3. Has a PT_LOAD program header
4. Kernel still boots to "user enter" via the SIMPLE.BIN
   path (so we don't regress the L60 milestone).

## Forward look

L67 - IRQ-driven IO / streamer redesign (probably). Once disk
reads are fast enough to comfortably load 2 MiB user programs
in CI, the regression test flips to BLANK.ELF and asserts
the "Hello world" loop reaches VGA.

L68 - keyboard IRQ re-enabled with PIC remap applied.
