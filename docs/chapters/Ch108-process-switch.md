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
