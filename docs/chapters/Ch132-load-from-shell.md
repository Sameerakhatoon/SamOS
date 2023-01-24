# Ch 138 - Load programs from the shell (cmd 6)

Adds a syscall that lets a user process launch another user process by file path. The shell uses it to make typed commands actually execute.

## What landed

Kernel side:
- `src/isr80h/process.{c,h}` - new TU. `isr80h_command6_process_load_start(frame)`:
  1. Pull `filename_user_ptr` off the user stack (`task_get_stack_item(..., 0)`).
  2. `copy_string_from_task(task_current(), filename_user_ptr, filename, sizeof filename)` - lift the C-string into kernel memory.
  3. Prefix with `"0:/"` to form a full path.
  4. `process_load_switch(path, &process)`.
  5. On success: `task_switch(process->task); task_return(&process->task->registers);` - never returns to the shell directly; the trap unwinds straight into the new program in ring 3.
- `src/isr80h/isr80h.h` - new `SYSTEM_COMMAND6_PROCESS_LOAD_START` enum value.
- `src/isr80h/isr80h.c` - registers cmd 6.
- `Makefile` - new build rule for `build/isr80h/process.o` and `FILES` updated.

User-space side:
- `programs/stdlib/src/samos.asm` - new `samos_process_load_start` routine wrapping `int 0x80` with `eax = 6`.
- `programs/stdlib/src/samos.h` - prototype.
- `programs/shell/src/shell.c` - after `samos_terminal_readline(buf, ...)`, the shell calls `samos_process_load_start(buf)`. Typing `BLANK.ELF` and hitting Enter loads and runs the blank program from inside the shell.

## Book "fixing a slight mistake" note

The book uses Ch 138 to add `res = -EIO;` inside the `if(res <= 0)` branch of `elf_load`'s fopen path. We already had that fix in place since Ch 124 (it was in our G-style "book quirk fixed inline" notes), so nothing to change here.

## Test impact

None. Suite stays 32/32. Manually verifiable: in QEMU, type "blank.elf" + Enter -> blank program runs and prints its "My age is 98" + while(1).
