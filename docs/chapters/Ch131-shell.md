# Ch 137 - Shell program

First standalone user program besides blank: a barebones shell. Prints a banner, then loops reading lines from the terminal. Command execution lands later.

## What landed

- `programs/shell/src/shell.{c,h}` - new program. `main` prints `SamOs v1.0.0\n` then loops `print("> "); samos_terminal_readline(buf, sizeof(buf), true); print("\n");`.
- `programs/shell/linker.ld` - same layout as blank's linker-elf.ld (`.text`, `.asm`, `.rodata`, `.data`, `.bss` aligned at 4 KiB).
- `programs/shell/Makefile` - mirrors blank's: compile shell.c -> shell.o, link with stdlib.elf -> shell.elf.
- Root `Makefile`:
  - `user_programs` and `user_programs_clean` recurse into `programs/shell`.
  - `all` mcopies `shell.elf` into the FAT volume.
- `src/kernel.c` - `process_load_switch` now loads `0:/shell.elf` instead of `blank.elf`; panic message updated to match.
- `programs/blank/blank.c` - simplified per book to just `printf("My age is %i\n", 98); while(1){}`. Blank is no longer the boot program; later the shell will spawn it.

## Naming note

Book uses `PeachOS v1.0.0`. We use `SamOs v1.0.0`.

## Test impact

`tests/26-fat16-resolver.sh` updated: FAT root dir is now 4 entries (`hello.txt`, `blank.bin`, `blank.elf`, `shell.elf`). `rdt=00000004` expected.

Suite stays at 32/32. The shell program itself is functional in QEMU - type at the prompt and press Enter to see input reflected back.
