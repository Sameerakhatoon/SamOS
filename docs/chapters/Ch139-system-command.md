# Ch 145 - 'system' command (real cmd 7)

The placeholder from Ch 143 turns into the real thing. User processes can now spawn other user processes BY NAME and pass them arguments - the moral equivalent of `system("ls -la")`. The shell uses it to make typed commands work with argv.

## What landed

Kernel side:
- `src/isr80h/process.c::isr80h_command7_invoke_system_command`:
  1. Pull `arguments` (a `struct command_argument*` from user space) and translate to physical via `task_virtual_address_to_physical`.
  2. Sanity-check non-null + non-empty argument string.
  3. `program_name = arguments[0].argument`. Prefix with `"0:/"` -> path.
  4. `process_load_switch(path, &process)`.
  5. `process_inject_arguments(process, arguments)` - the whole linked list.
  6. `task_switch(process->task); task_return(...)` - never returns.
- `src/kernel.c` - reverts to loading `0:/shell.elf`; the Ch 143 "Testing!" injection is gone. The shell is back in charge.

User side:
- `programs/stdlib/src/samos.asm` - `samos_system(struct command_argument*)` wrapping `int 0x80` with `eax = 7`.
- `programs/stdlib/src/samos.h` - exports `samos_system` + `samos_system_run`.
- `programs/stdlib/src/samos.c` - new `samos_system_run(command)`:
  ```c
  char buf[1024];
  strncpy(buf, command, sizeof(buf));
  root = samos_parse_command(buf, sizeof(buf));
  if (!root) return -1;
  return samos_system(root);
  ```
- `programs/stdlib/src/samos.c::samos_parse_command` - `scommand` buffer grows from 1024 to 1025 so commands can use the full 1024 bytes plus the NUL terminator.
- `programs/shell/src/shell.c` - swaps `samos_process_load_start(buf)` for `samos_system_run(buf)`. Now typing `BLANK.ELF foo bar` from the shell launches blank.elf with `argv[0] = "BLANK.ELF"`, `argv[1] = "foo"`, `argv[2] = "bar"`.
- `programs/blank/blank.c` - the new demo: walk `argv` and `printf("%s\n", argv[i])` for each.

## Test impact

Still 32/32. End-to-end demo (manual, not in suite): boot -> shell prompt -> type "BLANK.ELF arg1 arg2" + Enter -> blank.elf launches and prints each argv on its own line.
