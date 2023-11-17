# Lecture 171 - userspace relative graphics

Upstream: PeachOS64BitCourse 188dc3e.

## What landed

- `SYSTEM_COMMAND22_GRAPHICS_CREATE` in the syscall enum, and
  registered in `isr80h_register_commands`.
- `isr80h_command22_graphics_create` in `isr80h/graphics.c`.
  Pulls x/y/width/height plus the userland parent handle off
  the task stack, walks the sentinel to the kernel surface,
  calls `graphics_info_create_relative` for a child surface,
  then wraps the child in a fresh userland sentinel and
  `userland_graphics` mirror.
- Decl in `isr80h/graphics.h`.

The userspace `peachos_graphics_create` stdlib helper is not
ported; SamOs userland stays at its prior batch.

## BIOS test status

Source + link. Build links.
