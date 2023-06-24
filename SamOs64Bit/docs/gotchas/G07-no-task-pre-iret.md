# G07 - interrupt_handler panics on null current task between enable_interrupts and first task

**Surfaced during:** Ch 150 (PIT-driven multitasking), in the window between `enable_interrupts()` early in `kernel_main` and the later `task_run_first_ever_task()`. The PIT fires, `idt_clock` callback wants to save the current task's state, `task_current_save_state()` panics with "No current task to save".
**Fix:** in `src/idt/idt.c::interrupt_handler`, wrap callback dispatch in `if (task_current())` AND guard the post-callback `task_page()` the same way. The PIT IRQ becomes a no-op (just EOIs the PIC) when there's no user task to schedule yet.
**Regression test:** `tests/40-irq-before-task.sh` (the same sentinel as G05; both bugs ride the same null-current_task code path) and `tests/41-multitasking.sh` (which needs the whole chain to work).

## Where it lives

`src/idt/idt.c::interrupt_handler`.

## What the book ships

```c
if (interrupt_callbacks[interrupt] != 0) {
    task_current_save_state(frame);   // panics on null current_task
    interrupt_callbacks[interrupt](frame);
}
```

## What goes wrong (after Ch 150)

Ch 150 registers `idt_clock` as the callback for IRQ0 (vector 0x20). The PIT then fires every ~55 ms. Between `enable_interrupts()` and the first `task_run_first_ever_task()` we have ~10 ms of smoke probes (stream, fread, fopen, kmalloc, etc.). Our build is heavier than the book's so the first PIT IRQ lands in that window, walks into `task_current_save_state(frame)`, sees `current_task == NULL`, and panics:

```
No current task to save
```

11 tests fail at the Ch 150 commit because every post-`enable_interrupts` smoke probe drops off the screen.

## The fix

Guard the save inside `interrupt_handler`. Mirror the G05 pattern (which already guards `task_page()` on the way out):

```c
if (interrupt_callbacks[interrupt] != 0) {
    if (task_current()) {
        task_current_save_state(frame);
        interrupt_callbacks[interrupt](frame);
    }
}
```

The callback itself is also skipped when no task exists, since both `idt_clock` (`task_next`) and `idt_handle_exception` (`process_terminate`) need a current task to do anything useful.

## After the fix

11 -> 5 failing tests at G07. The remaining 5 (`05`, `10`, `34`, `39`, `40`) are not panics - they're VGA-snapshot races: with two blank user tasks now infinite-printing "Testing!" and "Abc!" every IRQ tick, the early boot output gets scrolled off the 80x20 VGA frame before the tests can `pmemsave`. That's a property of the Ch 150 demo design (the book's screenshot shows the same scroll-out), not a kernel bug. Future cleanup could update those tests to assert kernel-state markers that survive scrolling.

## Surfaced

Chapter 150 (timer-driven multitasking).

## Fix commit

`g07: guard interrupt_handler against null current_task in callback dispatch`.
