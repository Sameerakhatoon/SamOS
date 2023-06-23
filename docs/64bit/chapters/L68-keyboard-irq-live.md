# Lecture 68 - keyboard IRQ live

**Source commit (PeachOS64BitCourse):** `c857100`
**SamOs commit:** L68 on `module1-64bit` branch
**Regression test:** `tests64/L68-keyboard-irq-enabled.sh`

## Change

```c
#include "idt/irq.h"
...
int classic_keyboard_init(){
    idt_register_interrupt_callback(ISR_KEYBOARD_INTERRUPT, ...);
    keyboard_set_capslock(...);
    IRQ_enable(IRQ_KEYBOARD);    // <-- L68 NEW
    outb(PS2_PORT, PS2_COMMAND_ENABLE_FIRST_PORT);
    return 0;
}
```

Right after the IDT callback is registered, the PIC mask for
IRQ1 (keyboard) is cleared. The L61 mask was 0xFB master =
all-but-IRQ2-cascade masked; we now flip bit 1 to 0 -> mask =
0xF9, IRQ1 unmasked.

## Why now is safe

L52 re-enabled the full task-aware `interrupt_handler` (it
calls `kernel_page() + task_save + callback + task_page +
PIC ack`). `task_page()` derefs `current_task`. If the
keyboard IRQ fired BEFORE a user process was loaded, that
deref would crash.

By L66's flow, the boot sequence is:
1. kheap, paging, multiheap, IDT bring-up
2. fs / disk / kheap_post_paging
3. TSS allocate + install + ltr
4. isr80h_register_commands
5. **keyboard_init** -- IRQ_enable here
6. process_load_switch + task_run_first_ever_task

Step 6 sets current_task BEFORE the keyboard IRQ is actually
serviced (no preemption happens during step 6 either; we're
running with interrupts on, but step 6 doesn't yield). So
the first keystroke after task_return arrives with
`current_task != NULL`, and `task_page()` finds something to
switch to.

## kernel_main load target stays SIMPLE.BIN

Upstream's L68 flips the load target to "0:/shell.elf".
shell.elf is also a ~2 MiB ELF padded to its VMA -> same
PIO timing problem as L66's blank.elf. SamOs keeps
SIMPLE.BIN for CI speed; the manually-verified ELF path
still works.

## How we verified

VGA after L68:

```
...
tss ready
Loading program...
user enter
```

Same tokens as L66/L67. Pressing keys in an interactive
QEMU session now fires IRQs into our handler (manually
verified).

## Forward look

Module 1 is effectively complete. Module 2 (L100+) is graphics
and terminal. Between here and L100 the upstream commits are
EDK2 / UEFI work (L71-L80+) that doesn't apply to SamOs's
BIOS boot path.
