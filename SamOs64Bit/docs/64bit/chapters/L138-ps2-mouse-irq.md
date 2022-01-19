# Lecture 138 - PS/2 mouse IRQ handler + link

L138 lands `ps2_mouse_handle_interrupt`, adds the
`window_terminal` helper that L132's
`mouse_draw_default_impl` was already calling, and wires
`mouse.o` + `ps2mouse.o` + `build/mouse` into the build.

## Handler

State machine over `static uint8_t packet[4]` and
`static int packet_byte_count`:

1. Read one byte from `PS2_COMMUNICATION_PORT`.
2. If this is byte 0 and bit 3 is NOT set, drop (out of
   sync; that bit is always 1 in a real first byte).
3. Stash, advance.
4. When the packet is full (`mouse_packet_size` from the
   driver's private struct - 3 standard / 4 scroll-wheel):
   - `dx = (int8_t)packet[1]`
   - `dy = -(int8_t)packet[2]` (invert so up is positive on
     screen)
   - Buttons: bits 0/1/2 of `packet[0]`.
   - For scroll-wheel mice the wheel delta lives in
     `packet[3]`.
5. Compute new position, clamp against
   `screen->width/height - mouse.graphic.width/height`, call
   `mouse_position_set`.
6. Map the button bits to a `MOUSE_CLICK_TYPE`. Left dominates
   right; middle and scroll are read but currently dropped
   (the upstream `if(scroll && left && right && middle)`
   block is a no-op "supress warnings.").
7. Fire `mouse_click` if needed, then `mouse_moved`.

The "supress warnings" typo and the dropped middle/scroll path
are preserved verbatim.

## `window_terminal`

```c
struct terminal* window_terminal(struct window* window){
    return window->terminal;
}
```

Body terminal accessor used by the L132 default mouse draw.

## Build wiring

- `Makefile FILES` gains `mouse/mouse.o` and
  `mouse/ps2mouse.o` with recipes.
- `build.sh` gains `build/mouse` in the upfront mkdir list.
- mouse.c's L132 forward decls for `window_terminal` /
  `window_position_set` are removed; they would conflict with
  the window.h declarations now in scope.

## BIOS test status

Source + link. Test asserts the handler body, the
mouse_position_set / _click / _moved calls, the
`window_terminal` helper, and the new Makefile / build.sh
entries. Build links.
