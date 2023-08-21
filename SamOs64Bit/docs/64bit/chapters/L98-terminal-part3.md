# Lecture 98 - terminal part 3 (write path)

L98 lands the actual text-writing API on top of the L97
bookkeeping. After this commit a terminal can take a `char`,
draw it through the font system, and advance the cursor;
control characters route to the backspace and newline helpers.

## Public API added

- `terminal_write(t, c)` - one character. `\n` calls
  `terminal_handle_newline`; `0x08` calls `terminal_backspace`
  if the flag is set; everything else draws via `font_draw`
  and advances through `terminal_update_position_after_draw`.
- `terminal_print(t, s)` - a `while(*s)` loop around
  `terminal_write`.
- `terminal_backspace(t)` - cursor-relative erase. Steps the
  cursor back one cell (wrapping at column 0 to the previous
  row, and at row 0 to the bottom), then calls
  `terminal_restore_background` for that cell.
- `terminal_pixel_set(t, x, y, color)` - bounded
  `graphics_draw_pixel` wrapper.
- `terminal_draw_image(t, x, y, img)` - bounded
  `graphics_draw_image`.
- `terminal_draw_rect(t, x, y, w, h, color)` - bounded
  `graphics_draw_rect`. The rectangle helper itself is brand
  new (see "graphics_draw_rect stub" below).
- `terminal_transparency_key_set/_remove` and
  `terminal_ignore_color/_finish` - stubs. The
  surface-level setters they will forward to land in L99.

## `graphics_draw_rect` stub

L98 references a `graphics_draw_rect(g, x, y, w, h, color)`
that does not exist yet in upstream until L99. To keep the L98
link clean we add a no-op stub now and document the gap; L99
replaces the body. The header gets the prototype now so callers
can be wired without churning the header at L99 too.

## Symbol-collision fix

The legacy VGA-text terminal in `kernel.c` had a function called
`terminal_backspace(void)`. The new graphics terminal exports
`terminal_backspace(struct terminal*)`. Both link in, both are
called `terminal_backspace`, and the linker rejects.

The legacy VGA path has been dead since L74 (UEFI pivot dropped
the BIOS text-mode console). We rename the legacy function and
its one call site to `vga_terminal_backspace` to free the name
for the L98 graphics terminal. No behaviour change.

## BIOS test status

Source-only. Test asserts every L98 API symbol is declared and
defined, the `graphics_draw_rect` stub exists, the VGA backspace
rename landed, and the build links.
