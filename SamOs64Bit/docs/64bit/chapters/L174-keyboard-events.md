# Lecture 174 - keyboard events and handlers

Upstream: PeachOS64BitCourse f2fe438.

## What landed

`keyboard.h`:

- `KEYBOARD_EVENT_TYPE` enum and `struct keyboard_event` with a
  union of `key_press` / `caps_lock` payloads.
- `KEYBOARD_EVENT_LISTENER_ON_EVENT` callback type and
  `struct keyboard_listener`.
- `struct keyboard` gains `key_listeners` (vector of
  `struct keyboard_listener*`).
- API decls: `keyboard_default`, `keyboard_register_handler`,
  `keyboard_unregister_handler`, `keyboard_get_listener_ptr`,
  `keyboard_push_event_to_listeners`.

`keyboard.c`:

- `keyboard_insert` allocates the listener vector after a
  successful driver init. Upstream panics with
  `"Let the keyboard.c file make the key listeners vectors"` if
  the driver pre-populated the vector; preserved verbatim.
- `keyboard_push` now constructs a `keyboard_event` for each
  pressed key and fans it out via
  `keyboard_push_event_to_listeners` against
  `keyboard_default()`.
- `keyboard_default` returns the head of the inserted-keyboards
  list.
- `keyboard_register_handler` clones the caller's listener into
  kheap via `kzalloc` + `memcpy`, then pushes the heap pointer.
  NULL keyboard falls back to default. Returns -EINVARG if there
  is no default yet.
- `keyboard_get_listener_ptr` linear-scans by `on_event`.
- `keyboard_unregister_handler` pops and frees the registered
  clone.

## BIOS test status

Source + link. Build links.
