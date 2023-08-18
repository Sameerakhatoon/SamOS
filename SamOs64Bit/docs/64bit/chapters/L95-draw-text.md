# Lecture 95 - drawing text to the screen

L95 is the smallest possible "use the font system" commit:
load the system font during boot, draw one white "Hello world"
at the origin, and prove the L94 path composites.

## `font.c`

`font_draw_text` gains a NULL-font fallback: if the caller did
not specify a font, use `font_get_system_font()`. Most kernel
status text wants the system font, so threading the handle
through every call site would be pure noise.

## `kernel.c`

Two changes:

- `#include "graphics/font.h"`.
- After `gpt_init()`, call `font_system_init()` so the system
  font is loaded.
- After `keyboard_init()`, the L90 bkground.bmp draw is
  commented out and replaced by:

  ```c
  struct framebuffer_pixel white = {0};
  white.red = white.blue = white.green = 0xff;
  font_draw_text(graphics_screen_info(), NULL, 0, 0, "Hello world", white);
  graphics_redraw_all();
  ```

The bkground draw will return in L101 when the upstream walks
the screen with a black rectangle clear.

## What boots look like (when the disk image carries sysfont.bmp)

A black screen with "Hello world" in 9x16 white pixels at the
top-left. The L93 regional redraw fires once per glyph so the
text appears character by character if you slow execution down.

## Disk image

Upstream's Makefile copies `sysfont.bmp` to the FAT image; the
SamOs build pipeline does not yet do that, so this commit leaves
the system_font as NULL on first boot. `font_draw_text` then
notices the NULL `font` slot inside `font_draw_from_index` (via
the prelude bounds check inside the index path) and returns
without painting. No crash, no panic - the rest of boot
continues.

Wiring sysfont.bmp into the disk image is a follow-up task
captured in the L94 doc.

## BIOS test status

Source-only. Test checks the NULL-font branch, the include, the
init call, and the Hello-world line, then rebuilds.
