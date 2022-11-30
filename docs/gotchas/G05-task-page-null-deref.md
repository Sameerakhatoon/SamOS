# G05 - task_page() null-derefs on every IRQ before the first user task

## Where it lives

`src/idt/idt.c`, inside the Ch 113 `interrupt_handler`.

## What the book ships

```c
void interrupt_handler(int interrupt, struct interrupt_frame* frame){
    kernel_page();
    if(interrupt_callbacks[interrupt] != 0){
        task_current_save_state(frame);
        interrupt_callbacks[interrupt](frame);
    }
    task_page();           // <- crashes
    outb(0x20, 0x20);
}
```

`task_page()` is `user_registers(); task_switch(current_task); return 0;`. `task_switch` derefs `current_task` immediately (`paging_switch(task->page_directory)`).

## What goes wrong

`enable_interrupts()` runs in `kernel_main` before any process is loaded - so `current_task` is `NULL` when the first IRQ arrives. The chain is:

1. IRQ0 (timer) fires -> macro stub -> `interrupt_handler(0x20, frame)`.
2. No callback registered, so the `if` branch is skipped.
3. `task_page()` runs -> `task_switch(NULL)` -> read from address `~0` -> page fault.
4. Page-fault handler isn't installed yet (still using `interrupt_pointer_table[0xE]` stub) - and even if it were, it would crash for the same reason.
5. Triple-fault -> CPU reset -> QEMU's "no-reboot" mode halts the VM. Every smoke test that calls `enable_interrupts()` dies.

After ch113 ships, the suite goes from 31 passing to 4 passing. The 4 survivors are tooling/symbol tests that never start QEMU.

## How to surface it

Run any post-Ch 113 boot test before applying this fix - `09-kernel-main-runs.sh` is the cleanest reproducer because it just looks at EIP after `info registers` and finds the CPU back at the reset vector `EIP=0xFFF0` (or sees QEMU exit early).

## The fix

Guard `task_page()` so we only swap back to user paging when there's a task to swap to:

```c
if(task_current()){
    task_page();
}
```

When no user task is running, the kernel was already in its own page directory before the IRQ - no swap needed on the way out either.

## Surfaced

Chapter 113 (Implementing our generic interrupt handler).

## Fix commit

`g05: fix - guard task_page() in interrupt_handler against null current_task`.
