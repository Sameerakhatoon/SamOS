# Lecture 163 - isr80h get-window-event syscall

Upstream: PeachOS64BitCourse 00a06ae.

## What landed

- `SYSTEM_COMMAND18_GET_WINDOW_EVENT` in the syscall enum.
- `isr80h_command18_get_window_event` in `isr80h/window.c`.
  Takes a userland `struct window_event_userland*` off the task
  stack, translates the virtual address through the task's page
  tables, pops a kernel event off the per-process ring (L161),
  and feeds it through `window_event_to_userland` (L160) so the
  caller's buffer is filled.

## SamOs-specific note

SamOs returns the int status as `(void*)(intptr_t)res` so the
sign extends cleanly on 64-bit. Upstream `return res;` works
because their build accepts the implicit int-to-pointer cast.

## BIOS test status

Source + link. Build links.
