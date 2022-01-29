# Lecture 150 - fix runtime bugs in graphics + windows

L150 is the cleanup commit that retires several "Upstream bug
preserved" markers we have been carrying since L93, L99,
L116, and L141.

## Fixes landed

### `graphics_redraw_region` width clip

```c
if(local_x + width > g->width){
    width = g->width - local_x;     // was: return;
}
```

A wider-than-the-surface redraw now paints the covered
portion instead of giving up entirely.

### `graphics_redraw_region` child intersect

The L93 `intersect_right = MAX(...)` bug is finally
`MIN(...)`. The L93 doc warned that the bug was inert because
children were empty; now that L116 added real child surfaces
the fix lands.

### `graphics_transparency_key_set`

Writes to `graphics_info->transparency_key` (not
`ignore_color`). The L99 doc had the call-out.

### `graphics_get_child_at_position` forward branch

The forward-iteration left-edge check is `x >= child->starting_x`
(was `x > ...`), matching the top_first branch. Inconsistency
fixed.

### Fallback bounds check

The fallback `return graphics` rectangle test compares
`x < starting_x + width` (was `starting_y + width`). Almost-
always-wrong path now correct.

### Typo renames

- `GRAPHICS_FLAG_DO_NOT_OVERWRITE_TRASPARENT_PIXELS` is
  renamed to `..._TRANSPARENT_PIXELS` (drops the missing N).
- `grpahics_setup_stage_two` is renamed to
  `graphics_setup_stage_two` (drops the r/a swap).

Every L116/L141 call site is updated to the new spelling.

### kernel_main hook

```c
mouse_system_init();
mouse_system_load_static_drivers();
graphics_setup_stage_two(&default_graphics_info);
```

`mouse_system_init` lives in mouse.c since L132; the L150
upstream commit assumes it has already landed. Our port adds
the call explicitly since SamOs's kernel_main had no mouse
init before today. `mouse_system_load_static_drivers` gets a
header decl in mouse.h.

## BIOS test status

Source + link. Test asserts every typo is gone, every bug
fix is in the source, kernel.c calls the new init hooks, and
the build links.
