# Lecture 120 - window source part 2

L120 finishes `window_create`. Still source-only: `window.o`
is not in FILES because L121-L123 fill in
`window_draw_title_bar`, `window_set_z_index`, `window_focus`
that this body now calls (forward-declared at the top of
window.c).

## What lands

### Title bar + four borders

`graphics_info_create_relative` is called four times to build:

- `title_bar_graphics_info` at `(WINDOW_BORDER_PIXEL_SIZE, 0)`,
  `width x WINDOW_TITLE_BAR_HEIGHT`.
- `border_left_graphics_info` at `(0, WINDOW_TITLE_BAR_HEIGHT)`,
  `WINDOW_BORDER_PIXEL_SIZE x height`.
- `border_right_graphics_info` at the right edge.
- `border_bottom_graphics_info` along the bottom.

`title_bar_graphics_info` is stored on the window;
`border_left_graphics_info` is kept in the outer scope; the
right and bottom borders are SHADOW-declared inside the
if-block (upstream bug, preserved).

### Body surface + terminals

A `window_graphics_info` at `(window_body_width_offset,
window_body_height_offset)` for the body. Two terminals:

- `title_bar_terminal`: white foreground, no flags.
- `terminal`: red foreground, `TERMINAL_FLAG_BACKSPACE_ALLOWED`.

The body is filled white with `terminal_draw_rect`, then
snapshotted via `terminal_background_save` so the L98 backspace
path can restore it. If
`WINDOW_FLAG_BACKGROUND_TRANSPARENT` is set, white becomes the
terminal's transparency key.

### Close icon placement

`title_bar_components.close_btn` is computed:

- `icon_pos_x = title_bar_terminal.bounds.width - close_icon.width
   - close_icon.width / 2`
- `icon_pos_y = (title_bar_terminal.bounds.height - close_icon.height) / 2`

Stored on the window for hit-testing.

### Registration + initial paint

`vector_push(windows_vector, &window)`; `window_set_z_index`
gets `child_count + 1`; `window_focus` claims input; finally
`graphics_redraw_all` to commit the new surfaces.

### Error cleanup

The `out:` block walks back: free the body terminal, the title
bar terminal, vector_pop the window from the registry, and
kfree the struct.

## Upstream bugs preserved

1. **Shadow declarations**: `border_right_graphics_info` and
   `border_bottom_graphics_info` are re-declared inside the
   non-borderless branch, hiding the outer slots. The outer
   pointers stay NULL after the branch exits.

2. **Border draw triple-target**: the three `graphics_draw_rect`
   calls at the end of the non-borderless branch all paint
   `border_left_graphics_info` because the outer right / bottom
   slots are NULL. As a consequence the right and bottom
   borders never get filled; the left border gets painted three
   times. Inert today because `window.o` is not linked.

Both bugs are documented inline. A linear follow-up can demote
the inner declarations to assignments and re-target the draws.

## BIOS test status

Source-only. Test asserts every L120 line lands, the function
ends with `return window;`, window.o is still out of FILES, and
the build links.
