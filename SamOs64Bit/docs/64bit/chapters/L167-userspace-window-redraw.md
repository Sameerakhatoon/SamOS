# Lecture 167 - userspace window redraw

Upstream: PeachOS64BitCourse ac415ff.

## What landed

- `SYSTEM_COMMAND21_WINDOW_REDRAW` in the syscall enum.
- `isr80h_command21_window_redraw` body in `isr80h/window.c`:
  resolves a userland window handle and calls `window_redraw`.
- Decl in `isr80h/window.h`.

Upstream does not yet register the syscall in
`isr80h_register_commands` at this commit; the registration
lands at L170 ("Binding the window redraw to ISR80H"). SamOs
keeps the same staging so the audit trail matches.

## BIOS test status

Source + link. Build links.
