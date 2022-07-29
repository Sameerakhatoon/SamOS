# Ch 33 - Debugging Your 32-Bit Kernel

**Book pages:** 93-95 (Part 5)
**Code in this chapter:** none, procedure

## What the chapter is about

GDB stepping through the 32-bit kernel once it has been loaded by the boot sector. The boot sector loads `bin/kernel.bin` to physical 0x100000. The same code, with debugging symbols, lives in `build/kernelfull.o`. By telling GDB to overlay symbols from `kernelfull.o` at address 0x100000, breakpoints and step commands resolve to labels and source lines.

## How to attach

One terminal:

```bash
qemu-system-i386 -s -S -hda ./bin/os.bin
```

`-s` exposes a GDB stub on TCP 1234. `-S` halts the CPU until GDB tells it to continue.

Second terminal:

```bash
gdb
(gdb) add-symbol-file build/kernelfull.o 0x100000
(gdb) target remote localhost:1234
(gdb) break _start
(gdb) continue
```

`add-symbol-file build/kernelfull.o 0x100000` is the load-bearing step: it tells GDB "the symbols in this file describe code that lives at 0x100000". After this, `_start` resolves to the kernel's entry point, and `disassemble`, `info symbol`, etc. all work.

## What we can do once attached

- `stepi` to advance one machine instruction.
- `info registers` for full CPU state.
- `x/i $eip` to disassemble the next instruction.
- `x/16wx 0x100000` to dump the loaded kernel image.
- `break <symbol>` to pause execution at a label.

## Why this is not automated

The `tests/08-kernel-loaded.sh` test already verifies that EIP lands inside the kernel after boot. GDB stepping is a developer tool, not a regression check. The procedure is documented for whenever we need to chase a bug.

## Notes for SamOs

- We use `qemu-system-x86_64` not `qemu-system-i386` (both work; `x86_64` is just the superset). The book picks `i386` to match the kernel's 32-bit target. Either is fine.
- Use `layout asm` to see disassembly while stepping.
- Use `set architecture i386` if GDB defaults to something else.
