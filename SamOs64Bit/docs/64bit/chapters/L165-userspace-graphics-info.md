# Lecture 165 - userspace graphics info

Upstream: PeachOS64BitCourse 23dc6b3.

## What landed

- `src/isr80h/graphics.h` introduces `struct userland_graphics`
  (the userspace projection: x, y, width, height, a future
  pixels slot, and a `userland_ptr` sentinel) and the builder
  decl.
- `src/isr80h/graphics.c` implements
  `isr80h_graphics_make_userland_metadata`: wrap the kernel
  `graphics_info*` in a userland_ptr sentinel (L153),
  `process_malloc` a userland_graphics in the process address
  space, and copy width/height/relative_x/relative_y across.
- `SYSTEM_COMMAND19_WINDOW_GRAPHICS_GET` /
  `isr80h_command19_window_graphics_get` resolve the userland
  window handle to its kernel window, then call the builder
  against the window's body `graphics`. Userland gets a pointer
  it owns and must free.
- Makefile gains `isr80h/graphics.o` plus its compile recipe.

The userland stdlib helper `peachos_window_get_graphics` is not
ported; SamOs userland stays at its earlier batch.

## BIOS test status

Source + link. Build links.
