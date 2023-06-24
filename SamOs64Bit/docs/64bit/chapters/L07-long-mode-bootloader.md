# Lecture 7 - Rewrite kernel.asm to enter 64-bit long mode

**Source commit (PeachOS64BitCourse):** `91c2d33`
**SamOs commit:** *first commit on `module1-64bit` branch*
**Regression test:** `tests64/L07-long-mode-entry.sh`

## Why this chapter exists

SamOs runs in 32-bit protected mode. Every Lecture from this point
on assumes the CPU is in **64-bit long mode**: 64-bit registers,
4-level paging, RIP-relative addressing, the System V AMD64 calling
convention, etc. Lecture 7 is the surgical change that flips
mode - from now on `kernel.asm` itself is the bridge between the
16-bit BIOS world (still real-mode bytes at boot), the 32-bit
protected-mode world (where the bootloader hands us off), and the
64-bit long-mode world (where every C subsystem we re-port lives).

After this commit, nothing else from the kernel runs - the C
subsystems are temporarily out of the link (the Makefile's `FILES`
list is reduced to just `kernel.asm.o`). Lecture 8 brings
`kernel.c` back online in 64-bit. So Lecture 7 is graded purely on
"does the CPU actually reach long mode?"

## Theory primer

### 1. What "long mode" actually means

x86-64 long mode is the combination of three CPU states:

| State | Set by | Bit |
|---|---|---|
| **Long Mode Enable (LME)** - CPU is allowed to enter long mode | MSR `IA32_EFER` | bit 8 |
| **Long Mode Active (LMA)** - CPU is currently in long mode | MSR `IA32_EFER` | bit 10 (read-only, set by CPU) |
| **Paging on** - required by long mode | `CR0` | bit 31 (PG) |
| **PAE on** - required because long-mode paging uses 8-byte page-table entries | `CR4` | bit 5 (PAE) |

The handshake is:

1. Set `CR4.PAE = 1` (PAE must be ARMED before paging turns on).
2. Set `EFER.LME = 1` (long mode is now ARMED).
3. Set `CR0.PG = 1` (paging turns on).
4. The CPU sees PAE + LME + PG and **transitions** - it sets `EFER.LMA = 1` itself, and now expects 64-bit code segment descriptors.
5. `jmp 0x18:long_mode_entry` to a code segment whose descriptor has the **L bit** (long-mode bit) set. That far jump is the actual transition into 64-bit instruction decoding.

If you forget the far jump, the CPU stays in 32-bit-compatibility-
mode (sub-mode of long mode) and the next instruction byte gets
decoded as 32-bit code. If you forget PAE first, the `mov cr0` to
set PG raises `#GP`.

### 2. Why PAE is non-negotiable

32-bit paging uses 4-byte PDE/PTE - only 32 bits to encode a
physical address.

64-bit long mode needs to encode physical addresses wider than 32
bits, so it uses **8-byte entries** at every level. PAE is the
format that uses 8-byte entries. So PAE has to be on before
long-mode paging can interpret CR3 correctly.

### 3. Four levels of page tables

```
CR3
  └── PML4  (Page Map Level 4)
        └── PDPT  (Page Directory Pointer Table)
              └── PD  (Page Directory)
                    └── PT  (Page Table)  -- or 2 MiB leaf at PD with PS=1
```

Each level has 512 entries × 8 B = 4 KiB (one page). Address
translation chops the 48-bit canonical virtual address into four
9-bit indexes (PML4 / PDPT / PD / PT) and a 12-bit page offset.

This kernel uses PAGE_SIZE = 2 MiB at the PD level - the **PS bit
(bit 7) in a PD entry** turns that entry into a 2-MiB leaf,
skipping the PT layer. So our skeleton is PML4 → PDPT → PD-with-
PS=1. Three tables × 4 KiB = 12 KiB of paging RAM, which maps
the first 4 MiB of physical memory (two 2 MiB leaves).

For Lecture 7 that's plenty. Lecture 11 will switch to 4 KiB
pages and Lectures 12-32 will build the real allocator.

### 4. The GDT change

The old SamOs GDT had two entries: 32-bit code (0x08) + 32-bit
data (0x10). For long mode we need a **64-bit code segment** with
the **L bit** set in its flags byte:

| Selector | Type | Flag byte | Purpose |
|---|---|---|---|
| 0x00 | null | - | required |
| 0x08 | 32-bit code | `11001111b` | the segment we entered protected mode in; still active during the long-mode setup |
| 0x10 | 32-bit data | `11001111b` | DS / ES / SS during the setup |
| 0x18 | 64-bit code | `00100000b` (L=1) | the far-jump target |

The base / limit fields are ignored for 64-bit code segments - the
CPU just looks at the flags. We zero them.

### 5. CR3 must hold a physical address

When CR3 gets pointed at our `PML4_Table`, the CPU treats that as
a **physical** address. Since the kernel is linked to physical
0x100000 and runs identity-mapped, the value of `PML4_Table` as
the linker sees it (virtual address) equals the physical address.
This will stop being true once we relink the kernel at a higher
virtual base, but for now identity = ok.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/kernel.asm` | rewritten - drops `kernel_registers`, the A20 init, and the PIC remap (those come back later in 64-bit form); adds GDT + page tables + LM transition |
| `Makefile` | `FILES = ./build/kernel.asm.o` only; all toolchain refs switch to `x86_64-elf-*`; nasm uses `-f elf64`; the `user_programs` and `mformat` lines are temporarily dropped because there's no C kernel and no FAT16 image to populate |
| `build.sh` | `TARGET=x86_64-elf` instead of `i686-elf`; we keep `PREFIX=$HOME/opt/cross` because we already have both toolchains installed there side by side |

`boot/boot.asm` is unchanged from SamOs - it still loads 100
sectors at 0x100000 and far-jumps into our `_start`. Whether
`_start` is 32-bit code or 32-bit code that then drops to long
mode is invisible from the bootloader's perspective.

`linker.ld` is unchanged - `OUTPUT_FORMAT(binary)` doesn't care
about the bit-width, and our sections still sit from `1M` upward.

## How we verified

`tests64/L07-long-mode-entry.sh` boots the kernel under QEMU TCG,
sends the monitor `info registers` after ~1 s, and asserts:

- `CS = 0x0018` (we did transition to the 64-bit code segment)
- QEMU tags the CS line with `CS64`
- `CR0` bit 31 = 1 (paging on)
- `CR4` bit 5 = 1 (PAE on)
- `EFER` bit 10 = 1 (Long Mode Active - set by the CPU itself
  once it transitioned)

Observed values after the build:

```
CS  = 0018  CS64 [-R-]
CR0 = 80000011        (bit 31 set, plus the inherited PE bit 0)
CR4 = 00000020        (bit 5 set)
EFER= 0000000000000500 (bits 8 + 10 set: LME + LMA)
RIP = 000000000010004d (64-bit register dump = long mode confirmed)
R8..R15 visible in info registers
```

## Things that broke and are coming back later

Anything in `src/` that isn't `kernel.asm` is currently OUT of the
link. Includes every C subsystem we built in SamOs:

- `src/kernel.c` - Lecture 8 brings `kernel_main` back
- `src/idt/` - Lectures 35-38 rebuild the IDT for 64-bit
- `src/gdt/` - Lectures 49-51 build the C-side GDT + TSS code
- `src/memory/heap/`, `src/memory/paging/` - Lectures 10-32 rebuild
- `src/fs/`, `src/disk/`, `src/loader/`, `src/task/`, `src/isr80h/`,
  `src/keyboard/`, `src/string/` - Lectures 33-68 port each

So `bash tests/run.sh` from the 32-bit suite is meaningless on
this branch - those tests assume a working 32-bit user-mode demo.
We track 64-bit tests separately under `tests64/`.

## Commit

`module1-64bit` branch only - `main` stays at the working 32-bit
state. Whether to merge later, or keep them as parallel histories,
is a decision for after Module 1 is done.

## Next lecture

Lecture 8 - get back to `kernel.c`. The `long_mode_entry` stub
will `call kernel_main`, the Makefile will start compiling C files
again with `x86_64-elf-gcc`, and the linker will pull `kernel.o`
back into `kernel.bin`. By the end of Lecture 8 we have a kernel
that boots into long mode, transfers to C, runs some C, and halts.
