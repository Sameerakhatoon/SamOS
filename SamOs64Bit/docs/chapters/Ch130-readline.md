# Ch 136 - Read lines from terminal + getkey rename

User-space gains the ability to read a whole line of input (terminated by Enter, with backspace support and optional echo). The old `getkey` declaration is renamed to `samos_getkey` for naming consistency; new C-side helpers `samos_getkeyblock` and `samos_terminal_readline` join.

## What landed

- `programs/stdlib/src/samos.asm` - the assembly symbol `getkey` is renamed to `samos_getkey` (both the `global` directive and the label itself).
- `programs/stdlib/src/samos.h` - declarations for `samos_getkey`, `samos_getkeyblock`, `samos_terminal_readline`; adds `<stdbool.h>`.
- `programs/stdlib/src/samos.c` - NEW C source file (separate compilation unit from samos.asm):
  - `samos_getkeyblock()` - busy-loop on `samos_getkey()` until a non-zero key arrives.
  - `samos_terminal_readline(out, max, output_while_typing)` - capture chars into `out` until `\r` (0x0D = Enter) or max-1; echo back when `output_while_typing`; handle backspace (0x08) by decrementing the index and zeroing the previous char.
- `programs/stdlib/Makefile`:
  - Old `./build/samos.o` rule (which built from samos.asm) becomes `./build/samos.asm.o`.
  - New `./build/samos.o` rule builds from samos.c.
  - `-ffreestanding` added to the C flags.
- `programs/blank/blank.c` - replaces the `if(getkey())` loop with a single `samos_terminal_readline(buf, sizeof(buf), true); print(buf);` followed by `while(1){}`.

## Naming note

Book renames to `peachos_*`. We use `samos_*`.

## Test impact

32/32 stays green. User input via QEMU sendkeys still gets echoed and reflected back (the tests don't exercise this path actively, but the existing CPU-state checks all pass).
