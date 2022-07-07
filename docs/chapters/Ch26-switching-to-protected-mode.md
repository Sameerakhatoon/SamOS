# Ch 22 - Switching Our Kernel to Protected Mode

**Book pages:** 50-54 (Part 5)
**Code updated:** `src/boot.asm` rewritten, `src/data.asm` deleted, `build.sh` simplified
**Test:** `tests/05-enters-protected-mode.sh` (CPU register inspection)

## What we built

The bootloader now does a full Real Mode to Protected Mode transition:

1. Reverts `ORG` to `0x7C00` (the book's choice for this chapter, simpler than the Ch 8 `ORG 0` + segment trick).
2. Three-byte BPB stub: `jmp short start / nop` then 33 bytes of zeros.
3. `jmp 0:step2` clears CS to 0x0000 so segment-register state is known.
4. Real-mode segment setup: DS = ES = SS = 0, SP = 0x7C00.
5. `lgdt [gdt_descriptor]` loads our GDT.
6. Sets CR0 bit 0 (PE) via `mov eax, cr0 / or eax, 0x1 / mov cr0, eax`.
7. Far-jumps `jmp CODE_SEG:load32` which flushes the pipeline and starts 32-bit decoding under the new code selector.
8. In `[BITS 32]` block: reload DS, ES, FS, GS, SS with the data selector 0x10. EBP = ESP = 0x200000. Spin.

## The GDT

```
gdt_null:  8 bytes of zero               (selector 0x00, required)
gdt_code:  base=0, limit=0xFFFFF, G=1    (selector 0x08, kernel code)
gdt_data:  base=0, limit=0xFFFFF, G=1    (selector 0x10, kernel data)
```

Both code and data span 0 to 4 GiB (limit 0xFFFFF with granularity 4 KiB = 4 GiB). This is "flat" segmentation: segmentation gets disabled in spirit while remaining required by the CPU. Real protection later comes from page tables.

Access byte details:

```
gdt_code: 0x9A = 1001 1010
                P    = 1   (present)
                DPL  = 00  (ring 0)
                S    = 1   (code/data, not system)
                Type = 1010 = code, readable, not conforming

gdt_data: 0x92 = 1001 0010
                P    = 1
                DPL  = 00
                S    = 1
                Type = 0010 = data, writable, expand-up
```

Flags byte:

```
11001111b
G = 1     (limit in 4 KiB pages: 0xFFFFF * 4 KiB = 4 GiB)
D/B = 1   (32-bit operand/stack)
L = 0     (not 64-bit)
AVL = 0   (available for OS, we do not use it)
limit[19:16] = 0xF
```

## The GDT descriptor

```asm
gdt_descriptor:
    dw gdt_end - gdt_start - 1   ; limit (size minus 1)
    dd gdt_start                 ; linear base address
```

`lgdt` reads this 6-byte structure (2-byte limit, 4-byte base) and loads GDTR.

## What we observed via QEMU monitor

After boot the registers say:

```
CR0=00000011   (PE=1)
CS=0008  CS32  DPL=0  [-R-]
EIP=00007c7f   (spinning in load32)
```

So we did successfully reach 32-bit Protected Mode with the kernel code selector loaded.

## Quirks worth noting

- The book leaves the partition table region zeroed. `times 33 db 0` after the `jmp short / nop` reserves room where BIOS might write BPB-shaped data if it mistook us for a FAT partition.
- ESP is set to 0x200000 (2 MiB). Anything we push after this lands there. This is fine because we are not yet loading anything else into RAM below 2 MiB beyond the bootloader.
- `data.asm` from Ch 15 is removed; we are no longer reading sector 2 as a string. The kernel-loading sector reads come back in a later chapter.

## Why the test no longer checks visible output

After entering 32-bit mode the bootloader just spins (`jmp $`). Nothing is printed. The only observable state is the CPU registers. The test asks QEMU for `info registers` and verifies CR0.PE and CS match expectations.
