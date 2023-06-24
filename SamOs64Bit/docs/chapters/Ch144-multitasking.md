# Ch 150 - THE GRAND FINALE: timer-driven multitasking

PIT IRQ0 now drives round-robin task switching. Kernel boots two `blank.elf` instances with different argv ("Testing!" and "Abc!"); the timer interrupt switches between them so VGA shows their output interleaved.

## What landed

- `src/idt/idt.c`:
  - New `idt_clock()`: `outb(0x20, 0x20); task_next();`.
  - `idt_init` registers `idt_clock` as the callback for vector 0x20 (IRQ0 after PIC remap).
- `src/kernel.c::kernel_main` - the shell.elf load is replaced with TWO blank.elf loads:
  1. `process_load_switch("0:/blank.elf", &process); inject_arguments(... "Testing!")`
  2. `process_load_switch("0:/blank.elf", &process); inject_arguments(... "Abc!")`
- `programs/blank/blank.c` - infinite loop `while(1){ print(argv[0]); }`.

## Book bug shipped here

The book registers `idt_clock` in `idt_init`, which runs BEFORE `enable_interrupts`. Between `enable_interrupts` and the first `process_load_switch` (~10ms of kernel smoke probes in our build), `current_task` is still NULL. The first PIT IRQ in that window walks `interrupt_handler -> task_current_save_state` which panics with "No current task to save".

Our smoke probe path is heavier than the book's so we hit the window every time. The Ch 150 commit ships the panic; `g07` immediately after will guard `interrupt_handler` against the null-task case (mirrors the G05 fix).

## Test impact

11 tests fail at this commit (all post-`enable_interrupts` smoke probes drop off because the kernel panics before painting them). `g07` restores 32/32.
