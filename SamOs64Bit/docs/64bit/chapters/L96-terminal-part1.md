# Lecture 96 - terminal part 1

L96 introduces `src/graphics/terminal.{c,h}` source-only (no
Makefile change). The L96 `.c` references
`terminal_background_save` and `terminal_free`, which L97
adds. Linking the file now would fail; the upstream commit
gates the build the same way and we follow it.

## `struct terminal`

A text surface bound to a `graphics_info`. Fields:

- `graphics_info` - the surface the terminal paints on.
- `terminal_background` - pre-overdraw pixel snapshot used to
  restore the area at close. NULL at create time; L97 fills it.
- `text.row` / `text.col` - cursor position in glyph cells.
- `bounds.abs_x/y/width/height` - the rectangle the terminal
  owns on the surface, in pixels.
- `font` / `font_color` - the font used for rendering and the
  default colour for new characters.
- `flags` - bitmask. The only flag defined is
  `TERMINAL_FLAG_BACKSPACE_ALLOWED`.

## `terminal_create`

Allocates the struct, validates inputs (non-NULL font and
surface, in-bounds starting position), populates every field
in declaration order, calls `terminal_background_save` (L97),
and pushes the terminal onto a module-private vector for the
later compositor walk.

The forward-declared helpers live as `extern`s in the file so
the compile is clean even though the symbols are not provided
yet. Linking is not attempted at L96.

## `terminal_system_setup`

One-shot init that allocates the `terminal_vector`. Will be
called from `kernel_main` once the rest of the API exists.

## Coordinates

`terminal_abs_x_for_next_character` and the `_y_` sibling
project (row, col) cell coordinates to absolute pixel
coordinates on the bound surface. They are file-scope
`inline static` helpers used by the upcoming write path.

## BIOS test status

Source-only as usual. Test verifies both files exist, key
identifiers are present, `terminal.o` is NOT yet in `FILES`,
and the build still links.
