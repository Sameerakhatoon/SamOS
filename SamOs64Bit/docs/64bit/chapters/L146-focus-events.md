# Lecture 146 - focus / unfocus events

L146 closes the L121 focus path: after the title bar gets
repainted (red for focus, black for unfocus) and the surface
is redrawn, push a window event into the L123 handler chain
so listeners can react.

## window_unfocus

After the redraw, emit:

```c
struct window_event event = {0};
event.type = WINDOW_EVENT_TYPE_LOST_FOCUS;
window_event_push(old_focused_window, &event);
```

Replaces the L121 `TODO L146` comment.

## window_focus

After the full redraw, emit:

```c
struct window_event event = {0};
event.type = WINDOW_EVENT_TYPE_FOCUS;
window_event_push(window, &event);
```

## What this enables

L123's default per-window handler returns 0 for every event,
so nothing user-visible changes today. Future userland code
that registers a handler will see the focus transitions.

## BIOS test status

Source + link. Test asserts both event types fire from the
right function bodies, the L121 TODO is gone, and the build
links.
