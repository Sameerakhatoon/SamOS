# Ch 143 - Process command arguments (part 2, kernel side)

Mirror of Ch 142's user-space parser, now in the kernel. Adds the structures, helpers, and two syscalls so a spawned process can call back into the kernel and ask for the args it was launched with.

## What landed

Kernel side:
- `src/memory/paging/paging.{h,c}` - new `paging_get_physical_address(directory, virt)`. Walks the page directory + table to recover the physical address corresponding to a virtual one, preserving the in-page byte offset.
- `src/task/task.{h,c}` - new `task_virtual_address_to_physical(task, virt)` wrapping the above for a task's page directory.
- `src/task/process.h`:
  - `struct command_argument { char argument[512]; struct command_argument* next; };`
  - `struct process_arguments { int argc; char** argv; };`
  - `struct process` gains a `struct process_arguments arguments;` field.
- `src/task/process.c`:
  - `process_get_arguments(process, *argc, **argv)` - drains the stored argc/argv into out-pointers.
  - `process_count_command_arguments(root)` - walk the linked list to a count.
  - `process_inject_arguments(process, root)` - per-process malloc an argv table and copy each argument string into process-owned memory, then stash argc/argv into `process->arguments`.
- `src/isr80h/isr80h.h` - new `SYSTEM_COMMAND7_INVOKE_SYSTEM_COMMAND` (placeholder) and `SYSTEM_COMMAND8_GET_PROGRAM_ARGUMENTS`.
- `src/isr80h/process.{c,h}`:
  - `isr80h_command7_invoke_system_command` - placeholder, returns 0 (real body lands in Ch 145).
  - `isr80h_command8_get_program_arguments` - the user passes a `struct process_arguments*`. Kernel converts that user-virtual pointer to a kernel-physical one via `task_virtual_address_to_physical`, then writes argc/argv through it.
- `src/isr80h/isr80h.c` - registers cmds 7 and 8.
- `src/kernel.c` - temporarily switches back to loading `0:/blank.elf`, then injects a single argument "Testing!" via `process_inject_arguments` before `task_run_first_ever_task()`. Ch 145 reverts to shell.elf.

User side:
- `programs/stdlib/src/samos.asm` - `samos_process_get_arguments(struct process_arguments*)` wrapping `int 0x80` with `eax = 8`.
- `programs/stdlib/src/samos.h` - exports `struct process_arguments` + the new prototype.
- `programs/blank/blank.c` - asks for its own arguments and prints `"%i %s\n", arguments.argc, arguments.argv[0]`.

## Self-inflicted footgun fixed mid-chapter

Initial Write to `blank.c` silently no-op'd (a tool-side race with the Read state). The on-disk file still held the Ch 141 post-free demo (`ptr[0]='B'`), which combined with the per-PHDR mapping changes from Ch 126 turned into a tight #PF loop that masked itself as a getkey trap. Re-Writing blank.c with the Ch 143 body resolved it.

## Test impact

Still 32/32. VGA after boot now shows "1 Testing!" from the blank user program.
