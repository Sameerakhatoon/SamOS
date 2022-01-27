# Lecture 148 - move windows with the mouse + bugfix

L148 spans TWO upstream commits (the second is labelled
"#commit2"). We squash them into a single SamOs commit since
the second corrects an obvious bug in the first.

## `window_screen_mouse_move_handler`

If `window_moving` is set (the L147 title-bar click toggled
the drag state on):

1. Recentre the window so the cursor lands on the middle of
   the title bar:
   `abs_x = moved_x - title_bar.width/2`,
   `abs_y = moved_y - title_bar.height/2`.
   Call `window_position_set` with that.
2. Compute `(rel_x, rel_y)` against `root_graphics->starting_x/y`.
3. Push a `WINDOW_EVENT_TYPE_MOUSE_MOVE` with those coords.

## Upstream bug fixed (the `#commit2` half)

The first commit emitted the event push OUTSIDE the
`if(window_moving)` guard. With `window_moving == NULL` the
`window_moving->root_graphics` deref segfaults on every move.
The #commit2 patch moves the `rel_x / rel_y / event_push` lines
inside the if-block. We take the fixed shape directly.

## `window_title_bar_mouse_moved`

Stub. The dragging is driven from
`window_screen_mouse_move_handler`. Stays empty so the
title-bar move handler can be wired in `window_create` without
side effects.

## Wiring

`window_create` adds a single line after the L147 click
handler install:

```c
graphics_move_handler_set(title_bar_graphics_info, window_title_bar_mouse_moved);
```

## BIOS test status

Source + link. Test asserts:

- the `if(window_moving)` guard wraps the entire body;
- `window_position_set` lands;
- `WINDOW_EVENT_TYPE_MOUSE_MOVE` is emitted via
  `window_event_push`;
- the event push lives INSIDE the if-block (the
  `#commit2` bugfix);
- the title bar move stub is wired by `window_create`.

Build links.
