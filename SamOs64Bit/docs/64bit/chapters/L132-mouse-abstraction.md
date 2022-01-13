# Lecture 132 - mouse abstraction (source-only)

L132 adds `src/mouse/mouse.[ch]` source-only. Layer over the
L122 window system. A `struct mouse` is a driver instance
with init/draw callbacks, current screen coords, a graphic
window surface, and per-mouse click + move handler vectors.

## struct mouse

- `init` / `draw` callbacks (driver-supplied).
- `name[20]` for diagnostics.
- `coords.x / .y`.
- `graphic.window / .width / .height` - a borderless,
  click-through, background-transparent window pinned at
  `MOUSE_GRAPHIC_ZINDEX = 100000` so it stays above
  application windows.
- `event_handlers.click_handlers / .move_handlers` vectors.
- `private` slot for driver state.

## Public API

- `mouse_system_init` - allocates the driver vector.
- `mouse_system_load_static_drivers` - TODO; L137 will
  register the PS/2 driver here.
- `mouse_register(mouse)` - allocate the handler vectors,
  call `mouse->init`, centre the cursor on the screen, fall
  back to `mouse_draw_default_impl` (red 10x10 square) when
  no draw callback was provided, build the click-through
  graphic window, paint, and push onto the driver vector.
- `mouse_position_set(mouse, x, y)` - update coords and
  forward to `window_position_set` (L134).
- `mouse_click(mouse, type)` - iterate click handlers.
- `mouse_moved(mouse)` - iterate move handlers.

## Source-only

`mouse_register` calls `window_position_set` and
`mouse_draw_default_impl` calls `window_terminal`. Neither is
defined yet. We forward-declare them at the top of mouse.c so
the file compiles in isolation; linkage waits for L134 / L145.

`mouse.o` is NOT in FILES.

## BIOS test status

Source-only. Test checks both files exist, every enum / macro
/ typedef / struct field / public function is present, and
mouse.o is NOT yet in FILES.
