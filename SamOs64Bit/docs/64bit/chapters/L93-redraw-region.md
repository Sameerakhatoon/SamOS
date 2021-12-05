# Lecture 93 - regional redraw

L93 lets callers update a sub-rectangle of a surface without
repainting the whole thing. The new helpers are
`graphics_redraw_region`, `graphics_redraw_children`, and
`graphics_redraw_graphics_to_screen`. Together they let the font
path repaint one glyph cell instead of the entire screen.

## `graphics_redraw_region`

Inputs are a surface and a `(local_x, local_y, width, height)`
rectangle in that surface's coordinates. The body:

1. Bounds check the rectangle against the surface; bail early
   if `(local_x, local_y)` is off-surface or the rectangle
   overshoots width-wise.
2. Project to absolute screen coords
   `(dst_abs_x, dst_abs_y) = (starting_x + local_x, starting_y + local_y)`.
3. Call the now file-scope
   `graphics_paste_pixels_to_framebuffer` for the rectangle.
4. Walk every child, intersect the child's absolute rect with
   the redraw rect, and recursively redraw the intersection in
   the child's local coords.

The new helper is what L94's `font_draw_from_index` will call
under the name `graphics_redraw_graphics_to_screen`.

## Children walk

`graphics_redraw` now calls `graphics_redraw_children`, which
iterates `g->children` and calls `graphics_redraw` on each. The
L87 surface implementation had a TODO right where this code now
lives.

## Upstream bug preserved

`graphics_redraw_region` computes the intersection rectangle
with:

```c
intersect_right = MAX(child_abs_right, region_abs_right);
```

That should be `MIN`. The upstream commit ships it as `MAX`,
which means a child that ends BEFORE the redraw region will
still get redrawn out to the region's right edge - causing
out-of-bounds local_x in the recursive call when the difference
is large. We preserve the upstream form so the per-commit diff
stays clean; a follow-up "fix L93 intersect_right" can land
later without breaking the lecture mapping.

In practice the bug is hidden today because there are no child
surfaces (`children` vector is empty), so the loop body never
runs.

## `graphics_paste_pixels_to_framebuffer`

L87 made this `static` (file-scope). L93 promotes it to plain
file-scope (drops `static`) so `graphics_redraw_region` can
call it. No header export, no prototype outside `graphics.c`;
the only callers stay inside this file.

## kernel.h - MIN / MAX

Two header-only macros. Wrapped in `#ifndef` so future libc
shims or compiler intrinsics do not clash. Used by the
intersection math above.

## BIOS test status

Source-only. Test verifies the public API, the children walk,
the file-scope `paste_pixels`, the MIN/MAX macros, and that the
build still links.
