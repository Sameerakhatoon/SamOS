# Lecture 134 - window_position_set

L134 adds `window_position_set(window, new_x, new_y)` to
window.c. Moves the window's root_graphics surface, updates
the window struct, brings it to the top, calls
`graphics_info_recalculate` (L135 stub), computes the strip of
screen that just got uncovered, and asks the L93 regional
redraw to repaint only those pixels rather than the whole
screen. Finally calls `window_redraw` (L136 stub) to refresh
the window's own contents.

## Bounds clamping

If the requested position would push the window off the right
or bottom screen edge, `new_x` / `new_y` are clamped to
`screen.width/height - window.width/height - 1`. No clamp on
the left or top edge (size_t treats negatives as huge values
and the next bounds check short-circuits).

## Move-strip math

Two redraw strips are computed:

- **Horizontal**: width = `x_gap` (signed distance moved), at
  the trailing edge. Sign of `x_gap` flips the strip's x to
  the leading edge for left moves.
- **Vertical**: similar with `y_gap`.

If either strip ends up bigger than the window itself (which
the upstream algorithm can produce on a large diagonal move),
fall back to a single `graphics_redraw_region` over the OLD
root rectangle.

## Weak stubs for L135/L136

`graphics_info_recalculate` (L135) and `window_redraw` (L136)
are referenced here. We add `__attribute__((weak))` no-op
stubs in window.c so the build links at L134; L135/L136
replace the weak symbols with real bodies. (Both lectures land
the real symbols as non-weak globals which override.)

## window.h

Adds the prototype.

## BIOS test status

Source + link. Test asserts the prototype is in the header,
the body lives in window.c with bounds clamps + the
window_bring_to_top hook, and the two weak stubs exist.
The build links.
