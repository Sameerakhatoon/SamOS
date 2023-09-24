# Lecture 121 - window source part 3 (focus + draw title bar)

L121 fills three of the four helpers `window_create` (L120)
called out forward decls for: `window_draw_title_bar`,
`window_set_z_index`, `window_focus`. Plus the helpers
`window_unfocus` and the comparator `window_reorder` they lean
on. The last forward decl, `window_bring_to_top`, lands in
L122.

## `window_draw_title_bar(window, bg_color)`

1. `terminal_draw_rect` the entire title bar with `bg_color`.
2. Move the terminal cursor to (0, 0) and `terminal_print`
   the window title.
3. `terminal_ignore_color(white)`, `terminal_draw_image` the
   close icon at the computed position, then
   `terminal_ignore_color_finish`. White is the icon's
   transparent background, so ignoring it lets the bar colour
   show through.

## `window_reorder` + `window_set_z_index`

`window_reorder` is the `vector_reorder` comparator on
`struct window**`: `(win1->zindex < win2->zindex)` returns 1
when `win1` should come AFTER `win2`. Higher zindex sorts
later (the compositor draws back-to-front).

`window_set_z_index(window, zindex)` updates
`window->root_graphics`'s z via L116's `graphics_set_z_index`
and re-sorts the `windows_vector` so subsequent walks pick up
the new ordering.

## `window_unfocus(old)`

Repaints the old window's title bar black via
`window_draw_title_bar`, then asks the L93 regional redraw to
flush the screen rectangle under the window. Leaves a TODO for
the L146 `WINDOW_EVENT_TYPE_LOST_FOCUS` emission.

## `window_focus(window)`

Early-out when the window is NULL or already focused.
Otherwise:

1. Save the current `focused_window` into `old_focused_window`.
2. Set `focused_window = window`.
3. If `old_focused_window` has a title bar, `window_unfocus` it.
4. `window_bring_to_top(window)` (L122).
5. Paint the new focused window's title bar red.
6. `graphics_redraw_graphics_to_screen` the full root rectangle.

## What still does not link

`window_bring_to_top` is referenced; L122 adds it. window.o
stays out of FILES.

## BIOS test status

Source-only. Test asserts every new function definition,
ignore_color wiring, z-index call sites, and focus/unfocus
colour paints land. Build still links.
