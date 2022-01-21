# Lecture 140 - test the mouse: EOI both PICs + enable interrupts

L140 closes the L137-L139 mouse arc. Three small changes:

1. `no_interrupt_handler` writes `outb(0xA0, 0x20)` as well as
   the master `outb(0x20, 0x20)`. IRQ12 (PS/2 mouse) is on the
   slave PIC, so without that EOI the slave latch stays
   asserted and the controller stops raising further mouse
   IRQs.
2. `interrupt_handler` also EOIs the slave PIC; AND the
   `task_current_save_state` + `task_page` pair is commented
   out for the duration of the window-test boot path because
   the kernel never enters a user task in this branch and
   `task_page` would pagefault.
3. `kernel_main` calls `enable_interrupts()` after the test
   window is up so IRQ12 can actually fire.

## Bug carried with L140

The `task_current_save_state` / `task_page` commentary is
upstream's quick fix for the window-test boot path. Once a
later lecture restarts the user-program flow, both lines
should be uncommented again. The L140 boot is "kernel running
a window plus interrupts, no userland".

## BIOS test status

Source + link. Test asserts both PIC EOIs land in both
handlers, the save/restore pair is commented out, and
`enable_interrupts()` is in `kernel_main`. Build links.
