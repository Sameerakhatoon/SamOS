# Lecture 142 - graphics move handlers

L142 mirrors the L141 click-routing for mouse-move events.

## New functions

- `graphics_mouse_move(g, rel_x, rel_y, abs_x, abs_y)` -
  bubble up. Fires the surface's installed move callback,
  otherwise translates to parent-relative and recurses.
- `graphics_mouse_move_handler(mouse, abs_x, abs_y)` - mouse-
  subsystem callback. Resolves the screen position via
  `graphics_get_at_screen_position` (skipping the cursor
  subtree), translates to surface-relative coords, and calls
  `graphics_mouse_move`.
- `graphics_move_handler_set(g, fn)` - install a per-surface
  move callback.

## `grpahics_setup_stage_two`

Now also calls `mouse_register_move_handler(NULL,
graphics_mouse_move_handler)` so move events flow into the
same routing chain.

## SamOs deviation

Upstream's L142 `graphics.h` decl for
`graphics_move_handler_set` has a stray comma between the
function type and the parameter name:

```c
void graphics_move_handler_set(struct graphics_info* graphics,
                               GRAPHICS_MOUSE_MOVE_FUNCTION,
                               move_function);
```

That fails to compile (typedef + comma + identifier with no
type). We write the correct shape (`GRAPHICS_MOUSE_MOVE_FUNCTION
move_function`) and document the diff.

## BIOS test status

Source + link. Test asserts the three new symbols exist in
both .c and .h, the stage-2 init registers the move handler,
and the build links.
