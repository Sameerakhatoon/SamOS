# Ch 142 - Parsing process command arguments (part 1, user side)

User-space gets `samos_parse_command(command, max)`: splits a string into a linked list of `struct command_argument` nodes (each holds a 512-byte argument buffer + `next` pointer). Builds on top of `strtok` from Ch 139.

The shell will later call this to break `BLANK.ELF arg1 arg2` into three nodes before passing the list down to the kernel (Ch 143+).

## What landed

- `programs/stdlib/src/samos.h`:
  - `struct command_argument { char argument[512]; struct command_argument* next; };`
  - Prototype `struct command_argument* samos_parse_command(const char* command, int max);`.
- `programs/stdlib/src/samos.c`:
  - `#include "string.h"` for strtok/strncpy.
  - `samos_parse_command(command, max)` body:
    1. Reject if `max >= sizeof(scommand)` (1024). Conservative copy ceiling.
    2. `strncpy(scommand, command, ...)` so we tokenize a writable copy (strtok mutates).
    3. First `strtok(scommand, " ")` -> root; subsequent `strtok(0, " ")` walks.
    4. Allocate each node via `samos_malloc(sizeof(struct command_argument))` (now backed by mapped user pages thanks to Ch 140).

## Test impact

None. 32/32. The parser is callable but nothing in the suite drives it yet. Ch 143 wires the shell to use it.
