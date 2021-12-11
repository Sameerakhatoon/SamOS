# Lecture 99 - transparency keys, ignore colours, and rect fill

L99 finishes the surface-level colour filtering work the L87
struct had slots for and lands the real `graphics_draw_rect`
body, retiring the L98 stub.

## New / updated functions

- `graphics_draw_rect(g, x, y, w, h, color)` - column-major
  pixel walk. Calls `graphics_draw_pixel` per cell so the
  per-surface `ignore_color` filter applies.
- `graphics_ignore_color(g, color)` - sets `g->ignore_color`.
  Any subsequent `graphics_draw_pixel` whose source matches is
  silently skipped.
- `graphics_ignore_color_finish(g)` - resets `g->ignore_color`
  to black (the "no filter" sentinel).
- `graphics_transparency_key_set(g, color)` - intended to set
  `g->transparency_key`; see "Upstream bug preserved" below.
- `graphics_transparency_key_remove(g)` - clears
  `g->transparency_key`.

## Terminal wiring

The four terminal stubs from L98 (`terminal_transparency_key_set/_remove`,
`terminal_ignore_color/_finish`) become thin forwarders to the
surface-level functions. The function bodies are now one-liners.

## Upstream bugs preserved (documented inline)

1. `graphics_transparency_key_set` writes to
   `graphics_info->ignore_color` rather than
   `graphics_info->transparency_key`. The two slots mean
   different things: `ignore_color` is checked at draw time,
   `transparency_key` is checked during the composite pass. A
   set/remove pair around a transparent draw will therefore
   leave `ignore_color` polluted because `_remove` clears
   `transparency_key` (correctly) but not `ignore_color`.
2. `graphics_transparency_key_remove` resets only
   `transparency_key`. Paired with (1) this gives the
   set-then-clear cycle a one-way leak into `ignore_color`.

Both are flagged in `graphics.c` with explicit comments. The
fix is one line per function; we defer it so the per-commit
diff against upstream stays linear.

## BIOS test status

Source-only. Test asserts the real rect body (per-pixel walk,
not recursion), every new surface-level filter exists, the
terminal stubs route through, and the build links.
