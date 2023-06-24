# Lecture 37 - idt.c / idt.h widened to 64-bit

**Source commit (PeachOS64BitCourse):** `3bc381d`
**SamOs commit:** L37 on `module1-64bit` branch
**Regression test:** `tests64/L37-idt-c-64bit.sh`

## Why this chapter exists

The asm side is 64-bit (L35 + L36). The C side still has the
old 32-bit struct definitions and the 32-bit `idt_set` shape.
L37 widens both.

## Theory primer: 64-bit IDT gate

A 32-bit IDT gate is 8 bytes:

```
+0  offset_1     16  bits 0..15 of handler
+2  selector     16  GDT selector
+4  zero          8  reserved
+5  type_attr     8  present/DPL/gate-type
+6  offset_2     16  bits 16..31
```

A 64-bit IDT gate is 16 bytes:

```
+0  offset_1     16  bits 0..15
+2  selector     16  GDT selector (now points at the L=1 long-mode code seg)
+4  ist           8  Interrupt Stack Table index in the TSS (0..7; 0 = use current)
+5  type_attr     8  same meaning, different default
+6  offset_2     16  bits 16..31
+8  offset_3     32  bits 32..63
+12 reserved     32  must be zero
```

The IST field replaces the 32-bit "zero" byte and is the one
thing in 64-bit IDT specific to long mode: when nonzero, the
CPU swaps to a stack pulled from TSS.IST[ist]. Useful for
double-fault handlers where the current rsp might be bad. We
leave ist = 0 because we have no TSS yet.

`offset_3` is the high half of the now-64-bit handler address.

The `reserved` field at the end has to be written as zero or
the descriptor is malformed.

## type_attr 0xEE vs 0x8E

The 32-bit code installed 0xEE everywhere - present, DPL=3,
interrupt gate. DPL=3 means "user code can int X to trap into
this vector", which we want for int 0x80 but DON'T want for
CPU exceptions (a buggy user could `int 0xE` and pretend it's
a real #PF). L37's `idt_set` keeps 0xEE as the default and
flips to 0x8E (DPL=0) for vectors <= 0x31.

0x31 = 49 includes:

- 0..31: CPU exceptions (#DE, #DB, #NMI, ..., #VE, #CP)
- 32..47: PIC IRQ0..15 (after L46's PIC remap)
- 48..49: extra range (slightly broader than strictly needed
  but harmless - vector 48 is the local-APIC error vector on
  modern systems anyway)

Vectors >= 50 stay 0xEE so int 0x80 (= 128) is reachable from
user mode.

## idtr_desc.base widened

64-bit. The IDT is a static array in idt.c so its address is
wherever the linker dropped it - well below 4 GiB at the
moment, but uintptr_t makes the cast safe regardless.

## What's NOT in the build yet

idt.c references `task_current`, `process_terminate`,
`task_next`, `task_current_save_state`, `kernel_page`,
`task_page`. The task / process subsystem doesn't exist in
the 64-bit port yet - those calls are dead references in the
on-disk source. Adding idt.c to `FILES` would fail the link
on undefined symbols.

L38 stands the IDT up in the build - upstream likely stubs the
task references or comments them out. We'll find out when we
port L38.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/config.h` | adds `KERNEL_LONG_MODE_CODE_SELECTOR 0x18`. |
| `src/idt/idt.h` | `struct idt_desc` widened: `zero -> ist`, adds `offset_3`, `reserved`. `struct idtr_desc.base` widened to uint64_t. `struct interrupt_frame` GPR slots widened (rdi, rsi, ..., rax) to uint64_t. CPU-pushed frame fields widened. |
| `src/idt/idt.c` | adds `kheap.h` include. `idt_set` rewritten: KERNEL_LONG_MODE_CODE_SELECTOR, three-way offset split, ist=0, type_attr 0xEE / 0x8E. `idt_init` casts the base through uintptr_t to uint64_t. |

## How we verified

Same tokens as L32 - L36:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
```

idt.c isn't in the build, so this is a "tree still builds" check.

## Forward look

L38 wires the IDT in and tests it - probably with a test
interrupt that the kernel deliberately triggers. To get there
we'll need to either stub the task/process references in idt.c
or pull a minimal task/process scaffold in.
