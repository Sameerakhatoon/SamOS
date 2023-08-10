# Lecture 90 - drawing images to the screen

L90 connects the image decoder (L88) to the surface compositor
(L87). New helper `graphics_draw_image(g, img, x, y)` walks the
decoded `image_pixel_data` array and emits one
`graphics_draw_pixel` per source pixel into the destination
surface, with the top-left of the image placed at `(x, y)`. A
NULL surface routes to the root.

## Why route through draw_pixel

A faster path would be a `memcpy` per row, but that would skip
the `ignore_color` check `graphics_draw_pixel` performs. The
upstream pattern keeps the per-pixel call so any surface that
chooses a chroma-key (e.g. cursor sprites) gets it for free.

## kernel.c

The L87 red-square smoke is gone. After `disk_search_and_init`
and `keyboard_init`, `kernel_main` now:

```c
struct image* bg = graphics_image_load("@:/bkground.bmp");
if(bg){
    graphics_draw_image(NULL, bg, 0, 0);
    graphics_redraw_all();
}
```

`@:/` resolves to the primary FS disk (L81). A missing or
unreadable BMP is swallowed: boot continues to the user program.

## graphics.h cleanup

The header gains an `#include "graphics/image/image.h"` so
external callers do not have to include both. SamOs's previous
`graphics_image_formats_init` call from `graphics_setup` (added
in L88p2) matches the upstream L90 move; no further change
needed there.

## What still does not draw

Children. `graphics_redraw_all` calls `redraw` on the root and
the root has no compositor pass for child surfaces yet. The
background paints; once L92 starts adding text surfaces, those
will sit alongside in `children` and need the compositor.

## BIOS test status

Source-only. The test confirms the prototype + definition, the
header chain, and the kernel.c load + draw calls, and then
re-links the build.
