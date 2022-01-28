# Lecture 149 - fix compile errors with graphics + windows

L149 is a cleanup commit. Upstream fixes four issues:

1. Duplicate `graphics_mouse_click_handler` stub at the end of
   graphics.c (a leftover from L141).
2. Comma typo in the `graphics_move_handler_set` decl
   (`GRAPHICS_MOUSE_MOVE_FUNCTION, move_function` was missing
   the parameter type binding).
3. Missing `graphics_has_ancestor` body and decl - L145 used
   it but never landed the symbol.
4. Missing `window_event_push` decl in `window.h` so userland
   code in L153+ can push events.

## SamOs status

Items 1-3 were already resolved during earlier SamOs commits:

- L141 dropped upstream's duplicate stub at port time.
- L142 wrote the correct `graphics_move_handler_set` decl
  shape and documented the upstream comma.
- L145 added `graphics_has_ancestor` body + decl as part of
  the helpers commit.

L149 lands the remaining piece: `window_event_push` is added
to `window.h`. We also notice the L145 helpers
(`window_owns_graphics`, `window_focused`,
`window_get_from_graphics`, `window_redraw_region`,
`window_redraw_body_region`, `window_title_set`) had silently
slipped past their original port (the earlier Edit failed
without applying); we add them now too.

The test verifies all four upstream fixes are in place plus
the SamOs late-arrival header decls.

## BIOS test status

Source + link. Build links.
