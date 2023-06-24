# Lecture 8 - getting from kernel.asm back to kernel.c in long mode

**Source commit (PeachOS64BitCourse):** `7260655`
**SamOs commit:** Lecture 8 on `module1-64bit` branch
**Regression test:** `tests64/L08-kernel-c.sh`

## Why this chapter exists

Lecture 7 dropped us into long mode but parked at `jmp $`. Nothing
C-shaped happened. Lecture 8 closes that gap: at the end of
`long_mode_entry`, kernel.asm calls a 64-bit C function called
`kernel_main`. From this point on the kernel is **C with asm
helpers** instead of pure asm.

This is the smallest possible bridge - `kernel_main` is just
`while (1) {}`. The point isn't to do anything in C yet; the
point is to prove the asm → C handoff works in 64-bit long mode.
Every later lecture grows the C body lecture by lecture.

## Theory primer

### 1. Why we need a new data-segment reload in long mode

In Lecture 7 we filled DS / ES / FS / GS / SS with selector
`0x10` - the **32-bit** data descriptor. That was correct for the
asm bringup (the registers had to point at something while we
were still in 32-bit code). But once the CPU is in 64-bit mode,
the data-segment descriptors it expects look different:

| Long-mode data descriptor | Required encoding |
|---|---|
| Base, limit | ignored - CPU treats data segments as flat |
| L bit | 0 (only code segments care) |
| Access byte | type=10 (data) with RW=1 |

So we add a fourth GDT entry at selector `0x20` - a long-mode
data segment - and reload DS/ES/FS/GS/SS with it at the very top
of `long_mode_entry`. The 32-bit data segment at 0x10 stays in
the GDT (we still used it during the transition), it's just no
longer the active data selector.

The Intel manual is OK with a 32-bit data selector being loaded
into the segregs while running 64-bit code - in long mode the CPU
mostly ignores data-segment cached limits and bases - but
explicitly reloading with a long-mode descriptor is the clean
pattern. Doing it once at entry means every subsequent IRQ /
syscall handler can assume DS/ES/SS already match the kernel's
long-mode data view.

### 2. Stack setup in 64-bit

```nasm
mov rsp, 0x00200000
mov rbp, rsp
```

In Lecture 7 we did `mov ebp, 0x00200000 / mov esp, ebp` - that
set the **low 32 bits** of RBP/RSP but left the high 32 bits
undefined. Writing the full 64-bit register name (`rsp`, `rbp`)
forces a 64-bit immediate-extended write and zeroes the upper
bits.

If we left the high bits as garbage, the first `push` would
write to whatever physical address ended up at `RSP - 8` -
typically way outside the 4 MiB we've identity-mapped - and
trigger a `#PF`. So the explicit RSP/RBP load is mandatory.

### 3. The SystemV AMD64 ABI calling convention (in one paragraph)

When kernel.asm does `jmp kernel_main`, the System V AMD64 ABI
that gcc emits for `x86_64-elf` expects:

| Arg # | Register |
|---|---|
| 1 | RDI |
| 2 | RSI |
| 3 | RDX |
| 4 | RCX |
| 5 | R8 |
| 6 | R9 |

`kernel_main(void)` takes no args, so we don't preload anything.
The return register is RAX. Caller-saved (clobbered by the
callee): RAX, RCX, RDX, RSI, RDI, R8-R11. Callee-saved
(preserved by the callee): RBX, RBP, R12-R15.

For a kernel `_start` we use `jmp` rather than `call` because we
never expect to come back - if we used `call` and `kernel_main`
returned, the return address pushed on the stack would point at
asm code that doesn't exist. The fallback `jmp $` right after the
`jmp kernel_main` is a defensive park if a future kernel_main
DOES return.

### 4. Why we extended PD_Table by one more 2 MiB entry

Lecture 7's PD_Table mapped 4 MiB (two 2 MiB pages at
0x00000000-0x003FFFFF). The linker places kernel.bin starting at
`1M` (0x100000). The text section is small for now, but as soon
as we add a small kernel.o the relocatable kernelfull.o (which
contains kernel.asm's data sections plus kernel.c's text) gets
laid out such that `kernel_main` lands at 0x104000 with my build,
but page-aligned sections push other content further. Adding a
third 2 MiB leaf at 0x00400000 buys headroom - without it any
kernel data over 4 MiB total would page-fault.

PeachOS64 made the same change at this lecture.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/kernel.asm` | `extern kernel_main`; new `LONG_MODE_DATA_SEG = 0x20`; in `long_mode_entry`: reload DS/ES/FS/GS/SS with 0x20, set RSP/RBP to 0x200000, `jmp kernel_main`; GDT gains a 4th descriptor (long-mode data at 0x20); PD_Table grows from 2 to 3 entries |
| `src/kernel.c` | minimal stub - `void kernel_main(void) { while (1) {} }`. All the 32-bit kernel-init code stays in `main` branch history for reference |
| `Makefile` | `FILES = ./build/kernel.asm.o ./build/kernel.o` (kernel.o joins the link); the `./build/kernel.o: ./src/kernel.c` rule is reactivated with `x86_64-elf-gcc` |

## How we verified

`tests64/L08-kernel-c.sh`:

1. Builds the kernel.
2. Looks up `kernel_main`'s address via `x86_64-elf-nm
   build/kernelfull.o`. Adds the linker base (0x100000) to get
   the runtime address - `0x100000 + 0x4000 = 0x104000` on this
   build.
3. Boots the kernel under QEMU TCG, waits ~2 s, asks `info
   registers`.
4. Asserts long-mode invariants are still all set (CS=0x18,
   CR0.PG, CR4.PAE, EFER.LMA) AND `RIP == 0x104000`.

Observed:

```
RIP = 0000000000104000
CS  = 0018  CS64 [-R-]
CR0 = 80000011
CR4 = 00000020
EFER= 0000000000000500
```

`RIP == kernel_main` ⇒ the asm successfully jumped into C and
the C is spinning in its `while(1){}`. C-in-long-mode confirmed.

## What is still missing

- No console output yet - Lecture 9 brings the terminal back. For
  now there is literally no way for the kernel to produce a
  byte. We rely on the QEMU monitor + `info registers` for
  observability.
- No heap (Lecture 10), no paging API (Lectures 11-32), no IDT
  (Lectures 35-38), no GDT C side (Lectures 49-51).
- No user programs in the disk image - the Makefile's `mformat`/
  `mcopy` lines are still removed. Comes back when the C kernel
  needs to mount FAT16 at Lecture 84.

## Next lecture

Lecture 9 - restore simple terminal functionality. Reintroduce
`src/terminal/terminal.c` (PeachOS64 splits this out of
kernel.c) and let `kernel_main` print one banner before its
`while(1)`. Then we can observe progress via VGA pixels rather
than register dumps.
