# Lecture 97 - terminal part 2

L97 finishes the bookkeeping the L96 file deferred and adds
`terminal.o` to the build. It also retires the
`bits_height_per_chracter` typo: the field is renamed to
`bits_height_per_character` across `font.h`, `font.c`, and
`terminal.c`.

## Field rename

The L92 doc warned the typo would be preserved verbatim. L97 is
the upstream commit that drops it; we follow. `font.h::struct
font::bits_height_per_chracter` becomes
`::bits_height_per_character`, and every reader is updated. No
behaviour change.

## Terminal API additions

- `terminal_free(t)` - releases the snapshot buffer, pops the
  terminal from the registry, and frees the struct.
- `terminal_get_at_screen_position(x, y, ignore)` - first
  terminal whose bounds enclose `(x, y)`, skipping
  `ignore_terminal`. Used by the future input router.
- `terminal_background_save(t)` - lazily allocates and copies the
  surface's current pixel buffer into the restore snapshot.
- `terminal_restore_background(t, sx, sy, w, h)` - paints a
  saved-background sub-rectangle back through
  `graphics_draw_pixel` and asks the L93 regional redraw to
  flush. Used when overdrawing characters.
- `terminal_cursor_set / _row / _col` - get/set cursor in glyph
  cells, with `-EINVARG` on out-of-bounds.
- `terminal_total_cols / _rows` - bounds expressed in glyph cells.
- `terminal_bounds_check` - file-static predicate used by L98+.
- `terminal_handle_newline` and `terminal_update_position_after_draw`
  - file-static cursor advance helpers.

## Upstream bugs preserved (documented inline)

1. `terminal_get_at_screen_position` compares `x` against
   `terminal->bounds.width` (a size) instead of
   `terminal->bounds.abs_x + terminal->bounds.width` (the right
   edge). Same on y. Inert today because the function is not
   called.
2. `terminal_restore_background` overflow guard uses
   `terminal->bounds.width/height` instead of edges. Same
   pattern as (1).

Both are flagged in `terminal.c` with explicit "Upstream bug
preserved" comments referencing this doc.

## Build wiring

`build/graphics/terminal.o` joins `FILES` with the same recipe
shape as `font.o`. The build links.

## BIOS test status

Source-only. The test checks the typo rename succeeded, every
L97 public API symbol is declared and defined, terminal.o is in
`FILES`, and the build links.
