# Ch 112 - IDT macro refactor (interrupt_pointer_table + generic interrupt_handler)

Before: every IRQ needed a hand-rolled asm stub (int21h, no_interrupt) and a matching C handler. Adding a new interrupt meant editing both files.

After: idt.asm defines two NASM macros that mass-generate stubs + a lookup table, and idt.c exposes a single C entry point `interrupt_handler(int interrupt, struct interrupt_frame* frame)`.

## What landed

- `idt.asm`
  - Dropped `int21h` and its `extern int21h_handler`.
  - Added `extern interrupt_handler` and `global interrupt_pointer_table`.
  - Macro `interrupt %1` emits a `global int%1: pushad; push esp; push %1; call interrupt_handler; add esp,8; popad; iret` block.
  - Macro `interrupt_array_entry %1` emits `dd int%1`.
  - Two `%rep 512` loops: the first generates `int0`..`int511` stubs; the second packs their addresses into `interrupt_pointer_table` in the data section.
- `idt.c`
  - Removed `int21h_handler`.
  - `extern void* interrupt_pointer_table[SAMOS_TOTAL_INTERRUPTS];` pulls the table.
  - `idt_init` loop is now `idt_set(i, interrupt_pointer_table[i])` so every vector points at its macro-generated stub.
  - `idt_set(0x21, int21h)` deleted; the `idt_set(0x80, isr80h_wrapper)` and `idt_set(0, idt_zero)` overrides remain.
  - New `interrupt_handler` body just EOIs the master PIC (`outb(0x20, 0x20)`). Ch 113 fills in the real callback dispatch.

## Test impact

- `tests/11-keyboard-fires.sh` and `tests/12-keyboard-fires-repeatedly.sh` previously asserted the int21h handler's "Keyboard pressed!" message. That message is now gone. The tests are retargeted to verify the kernel survives IRQ1 storms (boot-time `bootsig=000055AA` smoke probe still visible after sendkeys). They'll get a fresh assertion once Ch 113 wires the classic driver's IRQ1 callback to push into the active process's keyboard buffer.

## Why the test rewrite, not a g-fix

The original int21h printout was a Ch 45 debug crutch (G01/G02). The book never restores it - it's superseded by the keyboard-buffer pipeline. So the right move is to update the tests, not to ship a regression g-fix that resurrects dead code.
