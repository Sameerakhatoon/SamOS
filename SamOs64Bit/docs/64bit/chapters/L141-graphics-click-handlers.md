# Lecture 141 - graphics click handlers

L141 adds a click-routing layer between the mouse subsystem
and individual surface event handlers.

## New API

- `graphics_click_handler_set(g, fn)` - install a per-surface
  click callback.
- `graphics_mouse_click(g, rel_x, rel_y, type)` - if `g` has a
  click handler, fire it; otherwise translate to parent-
  relative coords and recurse upward.
- `graphics_mouse_click_handler(mouse, x, y, type)` - mouse-
  subsystem callback. Resolves the screen position to a leaf
  graphics surface, skips the mouse cursor window's subtree
  via `graphics_is_in_ignored_branch`, then bubbles up via
  `graphics_mouse_click`.
- `graphics_get_at_screen_position(x, y, ignored, top_first)`
  - convenience wrapper for `_child_at_position` starting
  from `loaded_graphics_info`.
- `graphics_get_child_at_position(g, x, y, ignored, top_first)`
  - recursive walk that returns the leaf containing (x, y).
  `top_first` flips the children iteration so the front-most
  window wins.
- `graphics_is_in_ignored_branch(elem, ignored)` - walks
  `elem`'s parent chain looking for `ignored`.
- `grpahics_setup_stage_two(main)` - upstream-typo function
  (sic, "grpahics"). Registers
  `graphics_mouse_click_handler` with the mouse subsystem.

## Bookkeeping

`graphics.h` gains an `#include "mouse/mouse.h"` so
`MOUSE_CLICK_TYPE` is visible. `graphics.c` gains
`#include "graphics/window.h"` so `mouse->graphic.window` is a
complete type.

## Upstream bugs preserved verbatim

1. `graphics_get_child_at_position`'s fallback right-edge
   check uses `starting_y + width` rather than
   `starting_x + width`. Almost always wrong.
2. `top_first=true` uses `x >=` for the left edge;
   `top_first=false` uses `x >`. Inconsistent.
3. The function name `grpahics_setup_stage_two` (sic - r/a
   swap).
4. Upstream's "duplicate `graphics_mouse_click_handler`"
   stub later in the file is dropped; we only keep the
   complete body that arrives in the first hunk. Documented
   inline.

## BIOS test status

Source + link. Test asserts every new symbol exists in both
.c and .h, the mouse.h / window.h includes are added, and the
build links.
