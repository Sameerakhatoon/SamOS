# Lecture 175 - window keyboard listener

Upstream: PeachOS64BitCourse b318045.

## What landed

- A file-scope `window_keyboard_listener` (a
  `struct keyboard_listener` with on_event pointing at
  `window_keyboard_event_listener_on_event`).
- `window_system_initialize_stage2` now calls
  `keyboard_register_handler(NULL, window_keyboard_listener)`
  after the mouse handlers.
- `window_keyboard_event_listener_on_event` routes events to
  `window_focused()`, dropping when nothing is focused. Key
  presses become `WINDOW_EVENT_TYPE_KEY_PRESS` window events
  via `window_event_push`; caps-lock changes are a noop for now.

## Upstream bugs preserved as comments, not in code

Upstream wrote the registration as:

    keybaord_register_handler(NULL, window_keyboard_listener)

with a typo'd identifier (keybaord) and a missing semicolon.
Either alone would break the build. SamOs's port calls the
correctly-spelled `keyboard_register_handler` with the
semicolon and leaves a comment marking both upstream issues so
the audit trail records why our copy diverges.

## BIOS test status

Source + link. Build links.
