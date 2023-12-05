# Lecture 196 - fix keyboard events on userspace

Upstream: PeachOS64BitCourse 3726426.

## What landed

Single boot-order change: `keyboard_init()` is hoisted to
before `window_system_initialize_stage2()`.

L175 wired `keyboard_register_handler(NULL, window_keyboard_listener)`
in stage 2. With keyboard_init called later, `keyboard_default()`
returned NULL and the registration silently early-returned with
`-EINVARG`. The window-level listener never fired and userland
saw no keyboard events.

SamOs keeps the original call site as a comment so the audit
trail records the move; the live call is now up next to the
mouse-system-init block.

## BIOS test status

Source + link. Build links.
