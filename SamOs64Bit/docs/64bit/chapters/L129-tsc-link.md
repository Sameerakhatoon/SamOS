# Lecture 129 - tsc_microseconds (asm) + link + smoke

L129 adds the 128-bit arithmetic `tsc_microseconds` in
`src/io/tsc.asm`, fixes the L128 `tsc_miliseconds` paren bug,
puts both `tsc.o` and `tsc.asm.o` into `FILES`, and replaces
the L122 Test Window block with a 10-second udelay smoke loop.

## `tsc.asm`

The math `microseconds = current_tsc * 1_000_000 /
tsc_frequency` cannot use plain 64-bit C - the multiply
overflows once `current_tsc > ~1.8e13` (a few hours of
runtime).

x86_64 has the right primitives:

- `MUL r/m64` -> `RDX:RAX = RAX * source` (128-bit product).
- `DIV r/m64` -> `RAX = RDX:RAX / source`, `RDX = remainder`
  (128/64 divide).

The function:

1. `call tsc_frequency`, save at `[rbp-8]`.
2. `call read_tsc`, save at `[rbp-16]`.
3. `mov rax, [rbp-16]`; `mov rcx, 1000000`; `mul rcx` (the
   product spans RDX:RAX).
4. `mov rcx, [rbp-8]`; `div rcx` (RAX = microseconds).

Returns through plain `ret`. Stack discipline: `push rbp / mov
rbp, rsp / sub rsp, 32 / ... / add rsp, 32 / pop rbp`.

## tsc.c paren fix

`tsc_seconds` reads `tsc_miliseconds()` instead of
`tsc_miliseconds`. The L128 `(TIME_MILISECONDS)(uintptr_t)`
cast workaround is removed. Documented inline.

## Makefile

`build/io/tsc.o` + `build/io/tsc.asm.o` join `FILES`. Each
gets its own recipe.

## kernel.c

- `#include "io/tsc.h"`.
- The L122 `window_create("Test Window", ...)` + spin block is
  replaced with:

  ```c
  for(size_t i = 0; i < 10; i++){
      print("Another second\n");
      udelay(1000000);
  }
  int res = process_load_switch("@:/blank.elf", &p);
  ```

The L122 user-program restoration arrives at the bottom of the
loop. With the L128 frequency overshoot still live the messages
burst near-instantly (canary for L130 to address). The
user-program load is restored.

## SamOs deviation

Upstream's tsc.asm has `extern tsc_microseconds` next to
`extern read_tsc / tsc_frequency` AND `global
tsc_microseconds`. NASM rejects this as "label inconsistently
redefined" because the symbol is both extern (imported) and
global (exported). We drop the `extern tsc_microseconds` so
the symbol is purely global. Behaviour identical to what
upstream meant; the line was an editing slip.

## BIOS test status

Source + link. Test asserts tsc.asm exists with the right
label / globals / multiplier, the .c paren fix landed, both
objects are in FILES, and kernel.c calls udelay in the smoke
loop. Build links.
