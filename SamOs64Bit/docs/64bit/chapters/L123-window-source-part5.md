# Lecture 123 - window source part 5 (events + free + close)

L123 fills in the window event handler registry and the
teardown path: `window_event_handler_register`,
`window_event_handler_unregister`,
`window_drop_event_handlers`, `window_free`,
`window_event_push`, `window_close`, and the default
per-window `window_event_handler` stub.

## API

- `window_event_handler_register(window, handler)` -
  `vector_push` the handler.
- `window_event_handler_unregister(window, handler)` -
  `vector_pop_element` by handler pointer.
- `window_drop_event_handlers(window)` - loop:
  `vecotr_at` (sic) / pop / repeat until the vector is empty.
- `window_free(window)` - drop handlers, free the handlers
  vector, vector_pop_element the window from `windows_vector`,
  terminal_free both terminals, `graphics_info_free` the root
  (which recursively frees borders + body), kfree the struct.
- `window_event_push(window, event)` - stamp
  `event->window` and `event->win_id`, then iterate handlers
  calling each.
- `window_close(window)` - push a WINDOW_CLOSE event, free the
  window, redraw all.
- `window_event_handler(window, event)` - stub returning 0.

## Upstream typo preserved (with a workaround)

The upstream L123 commit ships `window_drop_event_handlers`
with `vecotr_at(...)` (sic, missing the second `t`). That
function does not exist in any vector library, so the upstream
file would not link. We keep the typo at the call site and add
a `static inline int vecotr_at(...)` wrapper that forwards to
`vector_at`. The diff against upstream stays clean and the
build links.

Documented inline with a "Upstream-typo wrapper" comment block.

## window_create wiring

The L120 TODO `Register the window event handler` is replaced
with:

```c
window_event_handler_register(window, window_event_handler);
```

So every window starts with one registered (stub) handler.

## window.h

Adds the register / unregister prototypes to the public API.
`window_drop_event_handlers`, `window_free`, `window_close`,
`window_event_push`, and `window_event_handler` stay
implementation-private for now.

## BIOS test status

Source-only. Test asserts every new function lands in
window.c, the upstream typo is preserved, the public API
declarations are in window.h, and the default handler is
registered inside `window_create`. The build links.
