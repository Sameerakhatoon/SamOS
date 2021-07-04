# Ch 139 - Memory + string functions in stdlib

Brings the kernel's memory/string utilities into user-space, plus new `isdigit`, `tonumericdigit`, and `strtok`. Sets up the next chapter's shell argv parsing.

## What landed

- `programs/stdlib/src/memory.{h,c}`:
  - `memset`, `memcmp`, `memcpy` - same shape as the kernel-side equivalents in `src/memory/memory.c`.
- `programs/stdlib/src/string.{h,c}`:
  - `tolower`, `strlen`, `strnlen`, `strnlen_terminator`, `istrncmp`, `strncmp`, `strcpy`, `strncpy` - mirror the kernel's `src/string/string.c`.
  - `isdigit(c)` - true for `'0'..'9'`.
  - `tonumericdigit(c)` - `c - '0'`.
  - `strtok(str, delimiters)` - stateful tokenizer using a file-scope `static char* sp` pointer; first call with non-null `str` seeds, subsequent calls pass null.
- `programs/stdlib/Makefile` - new build rules for `string.o` and `memory.o`; both go into `FILES`.
- `programs/blank/blank.c` - now demonstrates strtok by splitting `"hello how are you"` on space and printing each token via `printf("%s\n", ...)`.

## Naming note

Header guards use `SAMOS_MEMORY_H`, `SAMOS_STRING_H` per the project convention.

## Test impact

None. 32/32 stays. Tests don't exercise the new functions directly but the kernel still boots into the shell, and typing `BLANK.ELF` + Enter now shows the tokenized words.
