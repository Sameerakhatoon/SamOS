# Lecture 116 - essential graphical functions

L116 lands the supporting cast L118's window code will lean
on: child surface construction, z-order, surface-to-surface
paste, safe pixel reads, and a teardown that knows about
parent/child relationships and framebuffer ownership.

## New helpers

### `graphics_bounds_check(g, x, y)`

Pure predicate: `(x, y)` lies inside the surface's
`(width, height)` rectangle.

### `graphics_pixel_get(g, x, y, &pixel)`

Read one pixel into an output struct, bounds-checked. Returns
`-EINVARG` when out of bounds; success is 0 and `*pixel_out`
populated.

### `graphics_set_z_index(g, z)` + `graphics_reorder`

`graphics_reorder` is the vector_reorder comparator: returns
`first.z_index > second.z_index`. `graphics_set_z_index` writes
the new z and re-sorts the parent's children vector so the
compositor walks back-to-front.

### `graphics_paste_pixels_to_pixels(in, out, sx, sy, w, h, dx, dy, flags)`

Surface-to-surface (NOT surface-to-physical-framebuffer) copy.
Bounds-clip the source rectangle, optionally respect the
destination's `transparency_key` when
`GRAPHICS_FLAG_DO_NOT_OVERWRITE_TRASPARENT_PIXELS` is set
(upstream typo "TRASPARENT" preserved). Reads go through
`graphics_pixel_get` so per-pixel bounds checks apply.

### `graphics_info_create_relative(parent, x, y, w, h, flags)`

Allocate a child surface positioned at (parent.start + x,
parent.start + y), inherit the parent's framebuffer pointer
(NOT cloned: the `CLONED_FRAMEBUFFER` flag is cleared so the
free path does not double-kfree), and register on the parent's
children vector. Without `GRAPHICS_FLAG_DO_NOT_COPY_PIXELS` the
parent's pixels under the rectangle are pasted in to seed the
child.

`GRAPHICS_FLAG_ALLOW_OUT_OF_BOUNDS` disables the parent-rect
clip; useful for sprites that hang off the edge.

### `graphics_info_free(g)` + `graphics_info_children_free(g)`

Recursive teardown:
- Free children first.
- `kfree(pixels)`.
- `kfree(framebuffer)` only if `CLONED_FRAMEBUFFER` is set.
- Detach from parent via `vector_pop_element`.
- Skip `kfree(graphics_in)` when this IS the root surface
  (`loaded_graphics_info`).

`children_free` walks the parent's children vector. Each
`graphics_info_free` call vector-pops itself from the parent,
which mutates `vector_count(children)` mid-loop. Upstream
accepts the iterator drift; we follow.

## What this unlocks

L118 sketches a Window API on top of these primitives: each
window is a relative graphics_info under a desktop surface,
sized to its frame rectangle, with the title/content split as
nested children. z_index ordering lets the active window draw
last.

## BIOS test status

Source-only. Test asserts every new symbol is declared in
graphics.h and defined in graphics.c, and the build links.
