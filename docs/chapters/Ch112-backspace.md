# Ch 118 - Terminal backspace

Adds backspace handling. When `terminal_writechar` is called with ASCII 0x08 it now rubs out the previous character instead of literally drawing a 0x08 glyph.

## What landed

- `kernel.c::terminal_backspace`: walks the cursor back one cell (with row-wrap if we're at column 0), writes a space, walks the cursor back again so the next write lands on the rubbed-out spot. Both decrements matter - the first puts the cursor on the doomed char, `terminal_writechar(' ', 15)` overwrites it and bumps `terminal_col` forward, so the second decrement restores position.
- `kernel.c::terminal_writechar`: new branch `if (c == 0x08) { terminal_backspace(); return; }` right after the `\n` branch.

Nothing else changes. The user-land putchar pipeline already routes whatever character `keyboard_pop` returned, so as soon as our scan-code table maps the backspace key to 0x08 (index 0x0E in `keyboard_scan_set_one`, already in place), backspace just works from the user program.

## Edge case

If both `terminal_row` and `terminal_col` are 0 the function bails out - we treat that as "screen is empty, nothing to erase". This matches the book.
