# Lecture 53 - keyboard layer linked

**Source commit (PeachOS64BitCourse):** `4cd7e74`
**SamOs commit:** L53 on `module1-64bit` branch
**Regression test:** `tests64/L53-keyboard-linked.sh`

Upstream's L53 adds `keyboard.o` + `classic.o` (the PS/2 driver
"classic" keyboard backend) to FILES. SamOs follows.

No source-file changes. The keyboard code, like the fs and disk
layers in L46-L47, is pointer-width-clean and recompiles under
x86_64-elf-gcc without edits.

Nothing in `kernel_main` calls `keyboard_init` yet. The
keyboard IRQ (IRQ1 = vector 0x21) would route through the IDT
asm stub into `interrupt_handler`, which would look up
`interrupt_callbacks[0x21]` and find nothing. So even if we
enabled interrupts (we don't), keystrokes would be silently
acked at the PIC and dropped.

Wiring `keyboard_init` into kernel_main + enabling
interrupts is the next step, and requires:

1. A current_task exists (interrupt_handler's `task_page()`
   derefs current_task)
2. The TSS has been `ltr`'d (which itself requires reloading
   the GDTR with the larger limit so slot 7 is reachable)

These are the same prerequisites we noted at L50/L51/L52. A
future lecture will resolve all three.

## How we verified

Same tokens as L52. No change in runtime behaviour - the test
asserts the linker is happy.
