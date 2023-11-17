# Lecture 173 - userspace update window

Upstream: PeachOS64BitCourse 056275a.

## What landed

- `ISR80H_WINDOW_UPDATE_TITLE` subcommand enum in
  `isr80h/window.h`.
- `SYSTEM_COMMAND24_UPDATE_WINDOW` enum + registration.
- `isr80h_command24_update_window`: stack-arg dispatch on the
  update_type. Currently only `ISR80H_WINDOW_UPDATE_TITLE` is
  wired; default returns `-EINVARG`.
- Title-setter helper named `isr90h_command24_update_window_title`.
  Upstream typo (90h instead of 80h) preserved verbatim per the
  project rule on upstream identifiers; a code comment marks
  it explicitly.

## BIOS test status

Source + link. Build links.
