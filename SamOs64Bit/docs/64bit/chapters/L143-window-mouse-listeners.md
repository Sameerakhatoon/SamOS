# Lecture 143 - window mouse listeners + click handler

L143 hooks the window subsystem into the mouse subsystem.
`window_system_initialize_stage2` now registers two callbacks:

```
mouse_register_move_handler(NULL, window_screen_mouse_move_handler);
mouse_register_click_handler(NULL, window_click_handler);
```

## `window_screen_mouse_move_handler`

TODO stub: L144+ will drive window drags from here.

## `window_get_at_position(abs_x, abs_y, ignore_window)`

Walks `windows_vector`. For each candidate that is not
`ignore_window`, computes its root rectangle
`(starting_x, starting_y) .. (+width, +height)` and tests
whether (abs_x, abs_y) lands inside. First hit wins. Used by
the click handler with `ignore_window = mouse->graphic.window`
so the cursor sprite cannot click itself.

## `window_click_handler(mouse, abs_x, abs_y, type)`

1. Resolve the window under the cursor.
2. Translate to root-relative `(rel_x, rel_y)`.
3. Call `window_click(win, rel_x, rel_y, type)` (L144 lands the
   real body; a weak stub keeps the link clean here).
4. `window_focus(win)` so the title bar turns red.

## Weak `window_click` stub

L144 introduces the real body; without a stub at L143 the
link fails. We add `__attribute__((weak))` so L144's non-weak
definition silently overrides.

## BIOS test status

Source + link. Test asserts the three new symbols plus the
stage2 registrations land, the weak `window_click` stub is in
place, and the build links.
