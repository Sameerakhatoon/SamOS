# Lecture 162 - window event functionality for processes part 3

Upstream: PeachOS64BitCourse 5544005.

This is the lecture that actually wires the per-process event
handler into the window subsystem.

## What landed

Coordinate translation:

- `process_window_event_get_relative_window_body_coords` turns
  window-root coords into body-local coords. Rejects clicks that
  land outside the body.
- `process_window_event_modify_for_userspace_mouse_click` and
  `..._mouse_move` overwrite the event's click/move x/y in-place.
- `process_window_event_modify_for_userspace` is the type
  dispatcher.

Kernel-side event reactions:

- `process_current_set` is the trivial setter that
  `process_window_event_handler_event_focus` calls on a focus
  event, swapping the active process.
- `process_window_get_from_kernel_window` is the per-process
  variant of `process_get_from_kernel_window`.
- `process_window_event_handler_event_close` resolves the window
  to its process_window record and calls `process_window_closed`.
- `process_window_event_handler_kernel_handle_event` is the
  switch.

Top-level handler:

- `process_window_event_handler` looks up the owner, clones the
  event for body-relative coords, pushes the clone into the
  per-process ring (except for move events, which upstream
  silences for now), and then runs the kernel-side reactions
  through the unmodified event.

`process_window_create` now actually registers the handler:

    window_event_handler_register(proc_win->kernel_win,
                                  process_window_event_handler);

`process_window_closed` got a real body in this lecture: pop the
record from the process's windows vector, free the userspace
mirror, kfree the kernel record.

The L162 diff is dominated by upstream's pointer-style whitespace
sweep (`T* x` -> `T *x`); SamOs sticks with its own
`T* x` style throughout, so we only port the logic.

## BIOS test status

Source + link. Build links.
