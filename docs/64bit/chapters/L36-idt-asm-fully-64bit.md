# Lecture 36 - finish the idt.asm 64-bit migration

**Source commit (PeachOS64BitCourse):** `716f78d`
**SamOs commit:** L36 on `module1-64bit` branch
**Regression test:** `tests64/L36-idt-fully-64bit.sh`

## Why this chapter exists

L35 swapped pushad / popad for macros but left the rest of
idt.asm 32-bit. L36 finishes the migration in one go:

- `[BITS 64]` directive
- `idt_load` takes its arg in RDI (AMD64 SysV) instead of
  `[ebp+8]`
- `iret -> iretq` everywhere
- the per-vector `interrupt` macro passes the vector number +
  frame pointer via `rdi/rsi` instead of pushing them on the
  stack
- `isr80h_wrapper` does the same
- `tmp_res` storage `dd 0 -> dq 0`
- `interrupt_pointer_table` entries `dd int%1 -> dq int%1`

## Theory primer: 64-bit interrupt frames

In long mode, when the CPU vectors through an IDT entry it
ALWAYS pushes an 8-byte frame, regardless of the source's
privilege level. The 32-bit `iret` form would consume
4-byte slots and tear the stack apart. `iretq` is the 64-bit
form - pops 5 8-byte slots (rip, cs, rflags, rsp, ss).

The CPU also pushes ss and rsp UNCONDITIONALLY in long mode
(unlike protected mode, where same-DPL same-stack interrupts
skipped them). That makes the struct layout uniform between
kernel-mode interrupts and user-mode-via-int-0x80 syscalls:
every handler sees the same shape.

## Theory primer: SysV arg passing in the interrupt macros

Old (32-bit cdecl):

```nasm
push esp           ; arg 2: frame ptr
push dword %1      ; arg 1: vector number
call interrupt_handler
add esp, 8         ; clean up the two pushes
```

New (AMD64 SysV):

```nasm
mov rdi, %1        ; arg 1: vector number
mov rsi, rsp       ; arg 2: frame ptr
call interrupt_handler
```

No stack cleanup because args travel in registers. The C
handler sees `(uint64_t vec, struct interrupt_frame* frame)`
directly.

## Pointer-table widening: dd -> dq

`interrupt_pointer_table` is an array of function pointers.
With 8-byte function pointers in long mode, the entry
generator changes from `dd int%1` (4 bytes) to `dq int%1`
(8 bytes). idt.c's loop that walks the table needs to know to
stride by 8 bytes too - that comes in L37.

## extern int21h_handler

Upstream's L36 adds `extern int21h_handler` to the externs
list. Nothing in idt.asm actually references it - probably a
leftover from a planned keyboard-IRQ wire-up that will come
later. We mirror the extern for parity; an unreferenced
extern is harmless.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/idt/idt.asm` | full rewrite under `[BITS 64]`. New idt_load arg convention, iret -> iretq, SysV arg passing in the two handler macros, table entries widened. |

idt.asm is STILL not in the build - idt.c still has the
32-bit interrupt_frame struct + per-vector wire-up. L37
ports those; L38 puts the IDT into FILES.

## How we verified

Same VGA tokens as L32 - L35:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
```

idt.asm is on disk but not linked, so this is a "tree builds"
check.

## Forward look

L37 widens idt.c (struct interrupt_frame fields, the
interrupt_pointer_table loop's stride, idt_descriptor size).
L38 puts the IDT into the build and exercises it.
