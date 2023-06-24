# Ch 115 - PS/2 classic keyboard driver (part 2)

Ch 111 left `classic_keyboard_handle_interrupt` empty. This chapter fills it in and registers it as the IRQ1 callback so typed keys land in the active process's keyboard buffer.

## What landed

- `classic.h` gains three constants:
  - `CLASSIC_KEYBOARD_KEY_RELEASED 0x80` - high bit set on release scancodes
  - `ISR_KEYBOARD_INTERRUPT 0x21` - IRQ1 after PIC remap
  - `KEYBOARD_INPUT_PORT 0x60` - PS/2 data port
- `classic.c`
  - `classic_keyboard_handle_interrupt` body:
    1. `kernel_page()` - swap to kernel directory
    2. `scancode = insb(0x60)` - read scan code
    3. Second `insb(0x60)` to flush any ghost byte some hardware queues behind the primary code
    4. If `scancode & 0x80` (release) -> early return (skip the trailing `task_page()`; book-verbatim)
    5. Convert via `classic_keyboard_scancode_to_char`
    6. If non-zero -> `keyboard_push(c)` (writes into the current process's keyboard.buffer)
    7. `task_page()` - swap back to the user directory
  - `classic_keyboard_init` now also calls `idt_register_interrupt_callback(ISR_KEYBOARD_INTERRUPT, classic_keyboard_handle_interrupt)` before enabling the PS/2 port.

After this chapter, pressing a key in QEMU funnels the IRQ1 hardware event through int33 (macro stub) -> interrupt_handler -> classic_keyboard_handle_interrupt -> per-process keyboard buffer. Reading from that buffer is Ch 116's `getkey` syscall.

## Quirk

The early-return on release scancodes leaves the system in `kernel_page()` without ever flipping back via `task_page()`. The outer `interrupt_handler` does its own `task_page()` (post-G05 with a null guard) so the user task still sees the right page directory on resume. The book ships it this way.
