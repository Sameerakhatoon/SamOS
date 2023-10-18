# Lecture 145 - window helper functions

L145 adds a bunch of small helpers that L146+ will lean on.

## window.c

- `window_get_from_graphics(g)` - walk `windows_vector` for
  the window that owns `g` via `window_owns_graphics`.
- `window_owns_graphics(win, g)` - true when `g == win->root_graphics`
  or `g` has `win->root_graphics` as an ancestor.
- `window_focused()` - returns the `focused_window` module
  global.
- `window_redraw_region(win, x, y, w, h)` - regional redraw on
  the root surface.
- `window_redraw_body_region(win, x, y, w, h)` - regional
  redraw on the body surface only (skips title bar + borders).
- `window_title_set(win, title)` - strncpy the title, repaint
  the title bar black, full window redraw.
- `window_get_largest_zindex()` - returns the front-most
  window's zindex (vector index 0).
- `window_recalculate_zindexes()` - assigns every window a
  fresh zindex equal to `child_count + index + 1`.

## graphics.c

`graphics_has_ancestor(elem, ancestor)` walks `elem->parent`
looking for `ancestor`. Used by `window_owns_graphics`.

## Header

`window.h` exposes the new public helpers; the two
`window_get_largest_zindex` / `window_recalculate_zindexes`
helpers stay implementation-private (file-static could be next,
but for now they are file-global with forward decls at the top
of window.c).

## BIOS test status

Source + link. Test asserts every new symbol exists in both
.c and .h, `graphics_has_ancestor` lands in graphics, and the
build links.
