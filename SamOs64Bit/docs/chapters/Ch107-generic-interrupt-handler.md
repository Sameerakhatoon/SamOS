# Ch 113 - Generic interrupt_handler with callback table

Ch 112 left `interrupt_handler` as a stub that just EOIs the PIC. This chapter wires it to a per-interrupt C-callback dispatch table.

## What landed

- `idt.h`
  - `typedef void (*INTERRUPT_CALLBACK_FUNCTION)(struct interrupt_frame* frame);`
  - `int idt_register_interrupt_callback(int interrupt, INTERRUPT_CALLBACK_FUNCTION cb);`
- `idt.c`
  - `static INTERRUPT_CALLBACK_FUNCTION interrupt_callbacks[SAMOS_TOTAL_INTERRUPTS];`
  - `idt_register_interrupt_callback` validates the index against `SAMOS_TOTAL_INTERRUPTS` (returns `-EINVARG` out of range) and stores the pointer.
  - `interrupt_handler` body filled in:
    ```c
    kernel_page();
    if (interrupt_callbacks[i] != 0) {
        task_current_save_state(frame);
        interrupt_callbacks[i](frame);
    }
    task_page();
    outb(0x20, 0x20);
    ```

Any subsystem that wants to react to an IRQ now calls `idt_register_interrupt_callback(vector, fn)` once at boot and never touches asm again.

## Book bug shipped here

The body above is verbatim from the book. `task_page()` is called unconditionally on every IRQ, including ones that fire before the first user task starts running. `task_page()` itself does `task_switch(current_task)`, and when `current_task == NULL` that dereferences zero -> triple fault -> instant reset. After this chapter every smoke test that even reaches `enable_interrupts()` triple-faults.

The fix is in `g05` immediately following: wrap the `task_page()` call in `if (task_current())`.

## Files touched

- `src/idt/idt.{h,c}`
