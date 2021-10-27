# Ch 114 - process_switch + process_load_switch

Two tiny functions that point `current_process` at a loaded process. Together they let the kernel pick "the active process" - the one whose keyboard buffer IRQ1 should push into, whose pages task_page swaps to, etc.

## What landed

- `process.c`
  - `int process_switch(struct process* p) { current_process = p; return 0; }`
  - `int process_load_switch(const char* filename, struct process** p) { res = process_load(...); if(res==0) process_switch(*p); return res; }`
- `process.h` exports both prototypes.
- `kernel.c` swaps `process_load("0:/blank.bin", ...)` for `process_load_switch(...)`. The loaded process is now also the active one before `task_run_first_ever_task`.

## Why it matters

`process_current()` returned `NULL` until now because nothing ever set `current_process`. As soon as Ch 115's IRQ1 handler tries `keyboard_push(c)`, it calls `process_current()` and silently bails on the NULL. Wiring up `process_load_switch` in kernel_main is what makes the keyboard buffer actually receive keys in the next chapter.

## Why this chapter exists

Wires current_process so IRQ1 (and later G11) knows which buffer to use.

## How the change lands

process_switch + process_load_switch + kernel_main switch to load_switch variant.

## Regression test

tests/50 needs current_process == active task (post-G11).

## Commit

Original landing: ch114 process_switch + process_load_switch (see `git log --oneline` for the actual hash on your branch).
