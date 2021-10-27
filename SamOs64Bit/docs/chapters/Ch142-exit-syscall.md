# Ch 148 - exit syscall (cmd 9) + c_start calls it

Self-termination via int 0x80 cmd 9. User programs no longer need a trailing `while(1){}` to stay alive after `main` returns - the `c_start` bootstrap now calls `samos_exit()` once `main` is done, and the kernel cleanly tears the process down.

## What landed

Kernel side:
- `src/isr80h/isr80h.h` - new `SYSTEM_COMMAND9_EXIT` enum value.
- `src/isr80h/process.{c,h}` - new `isr80h_command9_exit`:
  ```c
  struct process* process = task_current()->process;
  process_terminate(process);
  task_next();
  return 0;
  ```
  (`task_next` never returns; the `return 0` is unreachable, kept for type symmetry.)
- `src/isr80h/isr80h.c` - registers cmd 9.

User side:
- `programs/stdlib/src/samos.asm` - `samos_exit` wrapping `int 0x80` with `eax = 9`.
- `programs/stdlib/src/samos.h` - exports `samos_exit`.
- `programs/stdlib/src/start.c::c_start` - calls `samos_exit()` after `main` returns.
- `programs/blank/blank.c` - drops the `while(1){}` since the bootstrap handles the post-main shutdown.

## Test impact

Still 32/32. Manual: typing `BLANK.ELF` from the shell now runs blank, prints its argv, returns, and the kernel quietly tears it down + lands the next task (the shell) back on screen.
