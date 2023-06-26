# Lecture 74 - kernel.asm fully UEFI-shaped

**Source commit (PeachOS64BitCourse):** `b438ced`
**SamOs commit:** L74 on `module1-64bit` branch
**Regression test:** `tests64/L74-kernel-asm-uefi-only.sh`

## Why this chapter exists

L73's UEFI bootloader hands control to the kernel ALREADY in
long mode. The kernel.asm's original prologue did the
32-bit -> 64-bit transition (PAE, CR3, EFER.LME, CR0.PG, far
jmp to L=1 CS). That dance is redundant under UEFI - the
firmware did the same thing before invoking the .efi.

L74 reduces `_start` to:

```nasm
_start:
    cli
    jmp long_mode_entry
```

and pushes ALL the bring-up logic into `long_mode_entry`.

## Three things long_mode_entry now does extra

```nasm
long_mode_entry:
    mov rax, PML4_Table     ; install OUR page tables
    mov cr3, rax            ; (replace UEFI's)

    lgdt [gdt_descriptor]   ; install OUR GDT
                            ; (replace UEFI's)

    mov ax, LONG_MODE_DATA_SEG
    mov ds, ax
    ...

    push QWORD 0x18                       ; far retfq to swap
    push QWORD long_mode_new_gdt_complete ; CS to slot 3
    retfq

long_mode_new_gdt_complete:
    ; PIC remap, jmp kernel_main
```

### Why our own CR3

UEFI's page tables mapped UEFI structures we don't care about
and likely don't include the kernel's stack (0x200000), the
boot-time PD_Table identity map, etc. Switching CR3 to our
PML4_Table makes the kernel address space deterministic.

### Why our own GDT (lgdt)

UEFI's GDT has different selector slots than what kernel.c
expects (we use slot 3 = long-mode code, slot 4 = data,
slot 5/6 = user, slot 7-8 = TSS). The lgdt installs OUR GDT
so those selectors point at the descriptors we built.

### Why the far retfq

`lgdt` only changes the GDTR; the live CS is still cached in
the CPU shadow register from UEFI's load. Until something
forces a CS reload (an iret, far jmp, far call, or far ret),
the CPU keeps using UEFI's CS slot.

`push QWORD 0x18; push QWORD label; retfq` is the atomic far
return: pops the target offset into RIP and the target CS
selector. After this the live CS is slot 3 = our long-mode
code seg.

## gdt_descriptor.base: dd -> dq

```nasm
gdt_descriptor:
    dw gdt_end - gdt - 1    ; limit (16 bits)
    dq gdt                  ; base (64 bits) - was `dd gdt`
```

In long mode the GDTR is `{u16 limit; u64 base}` - 10 bytes
total. The old `dd gdt` only filled the low 32 bits of the
base. Worked at boot when the GDT happened to live below
4 GiB but would tear if the kernel grew above.

## SamOs deviation: BIOS path BROKEN

Our boot.asm transitions to 32-bit protected mode and
`jmp CODE_SEG:0x100000`. The new kernel.asm's first byte is
`cli` (0xFA, same in both modes) but the second instruction
is a relative `jmp` to long_mode_entry which contains REX-
prefixed 64-bit instructions. In 32-bit mode the REX bytes
are interpreted as `inc`/`dec` opcodes, garbling the stream.

The kernel no longer boots under QEMU's `-hda` BIOS path. Our
regression test up to L73 sleep-and-dumped VGA; from L74 on
we only source-check.

To restore BIOS boot, two options:
1. Add a long-mode transition to boot.asm (mirroring the
   prologue we just deleted from kernel.asm).
2. Add a `_bios_start` entry point with the old 32-bit
   prologue, and have boot.asm jump to it instead of the
   linker's default _start.

Neither is in scope for L74; we follow upstream's UEFI-first
direction.

## Manual UEFI verification

When EDK2 is set up:
1. `cd /path/to/edk2 && . edksetup.sh && build`
2. The .efi is at `Build/MdeModule/DEBUG_GCC/X64/SamOs.efi`
3. Compose the GPT image via top-level `build.sh`
4. `./run.sh` (uses OVMF.fd)
5. UEFI shell shows "SamOs UEFI bootloader.", loads kernel.bin,
   jumps to 0x100000, kernel reaches kernel_main and prints
   "Hello 64-bit!" / "tss ready" / "user enter" as before.

## How CI verifies

`tests64/L74-kernel-asm-uefi-only.sh`:
- `[BITS 64]` at the top
- `_start: cli; jmp long_mode_entry`
- long_mode_entry contains `mov rax, PML4_Table`, `lgdt`, `retfq`
- `gdt_descriptor` uses `dq gdt`
- no stale `[BITS 32]`
- `bin/kernel.bin` builds

All pass.

## Forward look

L75 - re-detect free memory regions. The E820 BIOS call is
gone under UEFI; the UEFI memory map (gBS->GetMemoryMap before
ExitBootServices) replaces it.

L76 - unmapped kernel problem.

L77 - GPT partition table parsing.
