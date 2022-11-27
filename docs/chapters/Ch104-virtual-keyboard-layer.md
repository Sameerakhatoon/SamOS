# Ch 110 - Virtual keyboard layer

The book wires a thin virtual layer above whatever physical keyboard driver lands next chapter. Three pieces:

1. **Per-process ring buffer.** `struct process` gains an inline `struct keyboard_buffer { char buffer[SAMOS_KEYBOARD_BUFFER_SIZE]; int tail; int head; }` named `keyboard`. Each task carries its own typed-key buffer; the active process's tail moves forward as IRQ1 fires.
2. **Driver chain.** `struct keyboard { KEYBOARD_INIT_FUNCTION init; char name[20]; struct keyboard* next; }` plus `keyboard_list_head`/`keyboard_list_last` statics. `keyboard_insert` validates the driver has an `init`, appends to the list, then calls `init`. The PS/2 driver in Ch 111 will be the first inserted.
3. **Push/pop API.** `keyboard_push(c)` writes into the *current* process's buffer at `tail % SIZE`, then increments tail. `keyboard_pop()` reads from `head % SIZE`, returns 0 on empty slot, otherwise clears the slot and bumps head. `keyboard_backspace(process)` decrements tail and zeroes the new tail slot.

`kernel_main` calls `keyboard_init()` right after `isr80h_register_commands()`. The init function itself is empty for now - it exists so Ch 111's driver code has somewhere to plug in.

## Book quirk noted

`keyboard_pop` in the book ships with `if (task_current()) { return 0; }` - the condition is inverted and the function always returns 0 when a task exists. Shipped verbatim per the project's "follow the book even when it's wrong" rule. Ch 111+ will surface this once `getkey()` exists; a `gxx` commit will flip the test.

## Files touched

- `src/config.h` - `SAMOS_KEYBOARD_BUFFER_SIZE 1024`
- `src/keyboard/keyboard.{h,c}` - new
- `src/task/process.h` - `struct keyboard_buffer keyboard` member
- `src/kernel.c` - include + `keyboard_init()` call
- `Makefile`, `build.sh` - new build target / directory
