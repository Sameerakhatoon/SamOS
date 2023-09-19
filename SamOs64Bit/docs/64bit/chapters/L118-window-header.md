# Lecture 118 - Window header

L118 closes module 2 with the header that the (module-3)
window subsystem will implement. No window.c yet; the
header documents the contract.

## `struct window`

A window owns three surfaces and two terminals:

- `root_graphics` covers the whole window rectangle (frame +
  title bar + body). It is the child the parent compositor
  walks.
- `title_bar_graphics` is the strip across the top, child of
  `root_graphics`.
- `graphics` is the body region, child of `root_graphics`.
- `title_bar_terminal` and `terminal` are the L96-L100
  graphics terminals bound to those surfaces.

Position/size come from `(x, y, width, height)` and the
compositor draw order from `zindex`.

`title[WINDOW_MAX_TITLE]` is the displayed title; the macro is
new in `config.h`, set to 128.

## Title bar layout

`title_bar_components.close_btn` is a rectangle inside
`title_bar_graphics` reserved for the close affordance. The
shape is `(x, y, width, height)`; an event handler will hit-test
clicks against it.

## Events

```c
enum {
    WINDOW_EVENT_TYPE_NULL,
    WINDOW_EVENT_TYPE_FOCUS,
    WINDOW_EVENT_TYPE_LOST_FOCUS,
    WINDOW_EVENT_TYPE_MOUSE_MOVE,
    WINDOW_EVENT_TYPE_MOUSE_CLICK,
    WINDOW_EVENT_TYPE_WINDOW_CLOSE,
    WINDOW_EVENT_TYPE_KEY_PRESS
};
```

`struct window_event` carries the type, the source window
(both id and pointer), and a per-type union (`move.x/y`,
`click.x/y`, `keypress.key`; focus has no payload).

`WINDOW_EVENT_HANDLER` is `int (struct window*, struct
window_event*)`. `event_handlers.handlers` is a vector of
these callbacks; the window manager iterates them per event.

## Flags

```c
WINDOW_FLAG_BORDERLESS             = 1
WINDOW_FLAG_CLICK_THROUGH          = 2
WINDOW_FLAG_BACKGROUND_TRANSPARENT = 4
```

- BORDERLESS: skip drawing the frame and title bar.
- CLICK_THROUGH: do not consume mouse events.
- BACKGROUND_TRANSPARENT: do not fill the body before
  composite (the compositor's transparency_key applies).

## Module 2 end

This is the last upstream lecture in module 2. The kernel can
now (in source):

- Boot through UEFI, set up graphics, render a system terminal.
- Run userland C programs via `fopen / fread / fseek / fstat /
  fclose / malloc / free / calloc / realloc`.
- Manage processes through a vector-backed slot table.
- Manage paging descriptors per process (L113).
- Compose surfaces with z-order, transparency, regional redraw,
  and child surfaces (L116).
- Express a window contract (this lecture).

## BIOS test status

Source-only header drop. Test verifies the header exists, the
config macro, every enum + flag + struct field is present, and
that the build links. window.c is asserted absent (lands later).
