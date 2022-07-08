# Ch 23 - Stepping Through the Protected Mode Transition with GDB

**Book pages:** 55-57 (Part 5)
**Code in this chapter:** none, procedure
**Status:** documented, not part of automated tests

## What the book wants us to do

Watch the CR0.PE flip and the CS reload happen instruction by instruction in GDB. Useful for understanding the moment Real Mode ends and Protected Mode begins.

## Driving QEMU as a GDB target

In one terminal:

```bash
qemu-system-x86_64 -s -S -hda bin/os.bin -m 16 -accel tcg -display none -no-reboot
```

- `-s` opens a GDB stub on TCP port 1234.
- `-S` halts the CPU before the first instruction so we have time to attach.

In another terminal:

```bash
gdb
(gdb) target remote localhost:1234
(gdb) layout asm
(gdb) break *0x7c00
(gdb) continue
(gdb) stepi
```

`layout asm` shows the disassembly. The disassembler defaults to 32-bit so the first few instructions show garbage (they are 16-bit). Once we cross the far jump into `load32` the disassembly aligns and we see the real 32-bit instructions.

Useful commands while stepping:

```
info registers          # all CPU state
info registers cr0      # just CR0 (look for bit 0 = PE)
x/16wx 0x7c00           # peek at memory
x/i $eip                # disassemble next instruction
```

## What to look for

- Before the GDT load: `CR0=00000010` (PE = 0), all segment registers hold real-mode segment bases.
- After `mov cr0, eax` but before the far jump: `CR0=00000011` (PE = 1) but CS still holds the real-mode value. The CPU is in a half-state which is why the very next instruction must be a far jump.
- After the far jump: `CS=0008` (our kernel code selector), the CPU is decoding 32-bit instructions, and EIP points just inside `load32`.

## Why we are not automating this

A boot test for "the transition happens correctly" already exists: `tests/05-enters-protected-mode.sh` asks QEMU monitor for the post-boot register state and checks CR0.PE and CS. That covers the same evidence the GDB walk through produces, without needing a human at the keyboard. GDB is here as a learning tool, not a regression check.
