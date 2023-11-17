# Lecture 172 - userspace redraw region

Upstream: PeachOS64BitCourse 82ea8f2.

## What landed

- `SYSTEM_COMMAND23_WINDOW_REDRAW_REGION` in the enum,
  registered in `isr80h_register_commands`.
- `isr80h_command23_window_redraw_region` in
  `isr80h/window.c`: takes x/y/width/height plus the userland
  window handle off the task stack, resolves the handle, and
  forwards to `window_redraw_body_region`. Returns
  `ERROR(-EINVARG)` on bad args.
- Decl in `isr80h/window.h`.

## BIOS test status

Source + link. Build links.
