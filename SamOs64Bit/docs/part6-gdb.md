# Part 6 - GDB debugging cheatsheet

The book's Part 6 isn't more chapters - it's a quick GDB reference for when you need to step through the kernel. Captured here verbatim so it stays with the repo.

## Setup

```bash
cd ./bin
gdb
(gdb) add-symbol-file ../build/kernelfull.o 0x100000
(gdb) target remote | qemu-system-i386 -hda ./os.bin -S -gdb stdio
(gdb) c        # continue once you've set breakpoints
```

The `-S` flag halts the CPU until GDB tells it to continue; the `-gdb stdio` sends qemu's gdbserver over the same pipe. add-symbol-file at `0x100000` matches where boot.asm loads kernel.bin.

## Helpful commands

| Command | What it does |
|---|---|
| `info registers` | dump every register (incl segment regs, CR0/2/3, EFLAGS) |
| `x/i $pc` | disassemble the instruction at the current PC |
| `break *0x00100000` | breakpoint at a raw address |
| `break symbolname` | breakpoint on a symbol (needs symbols loaded) |
| `continue` (`c`) | resume execution |
| `stepi` (`si`) | single-step one machine instruction, step INTO calls |
| `nexti` (`ni`) | single-step, step OVER calls |
| `next`  (`n`)  | step one C source line, step OVER calls |
| `layout asm` | TUI disassembly view |
| `layout source` | TUI C source view |

## SamOs-specific add-symbol-files

For our build, after qemu-system-i386 is connected, you'll usually want symbols for both kernel and the running user program. Replace `<PID>` with the slot index if you want to inspect a specific blank instance.

```
(gdb) add-symbol-file ../build/kernelfull.o 0x100000
(gdb) add-symbol-file ../programs/blank/blank.elf 0x400000
(gdb) add-symbol-file ../programs/shell/shell.elf 0x400000  # if shell.elf is the active task
```

Two user programs at the same VMA: only one set of symbols is "live" at a time, the active one (`task_current()->process`) is whichever was last switched to by `idt_clock` or `task_switch`.

## Part 7

The book's Part 7 is a bonus section with a video course pitch and a closing letter. Skipped per the project's scope.
