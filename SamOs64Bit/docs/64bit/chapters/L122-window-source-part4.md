# Lecture 122 - window source part 4 (link + first window)

L122 lands the last forward-decl (`window_bring_to_top`),
adds the public window API to `window.h`, wires `window.o`
into the build, and modifies `kernel_main` to create a "Test
Window" and spin so the L119-L122 chain can be observed
end-to-end under OVMF.

## `window_bring_to_top`

After L121's `vector_reorder`, the last child of the screen's
children vector is the topmost window. `window_bring_to_top`
peeks that child's `z_index`, stamps the supplied window with
`that + 1` via `window_set_z_index`, and the sort puts it at
the very end.

If the children vector is empty, `last_index` defaults to 0
and the new window lands at z=1.

## `window.h` public API

Adds:

```c
int             window_system_initialize(void);
int             window_system_initialize_stage2(void);
void            window_set_z_index(struct window*, int zindex);
void            window_unfocus(struct window* old_focused_window);
void            window_focus(struct window* window);
struct window*  window_create(...);
```

`window_bring_to_top`, `window_draw_title_bar`, and
`window_reorder` stay internal to the implementation.

## Makefile

`build/graphics/window.o` joins `FILES` with a recipe matching
`graphics.o`. The clean build links.

## `kernel.c`

- `#include "graphics/window.h"`.
- After `terminal_system_setup()`: call
  `window_system_initialize()` then
  `window_system_initialize_stage2()`.
- After the disk + font + system terminal are up, the old
  `process_load_switch("@:/blank.elf", ...)` +
  `task_run_first_ever_task()` pair is commented out. In its
  place:

  ```c
  struct window* win = window_create(graphics_screen_info(), NULL,
                                     "Test Window",
                                     50, 50, 300, 300, 0, 4395327);
  (void)win;
  while(1){
  }
  ```

The window covers a 300x300 rectangle at (50, 50). With the
auto-id path it would have stamped its own id; passing
`4395327` instead lets us hard-code a recognisable value.

## What boots look like

Under OVMF: black screen, then a single window with a black
title bar and a white body. The L121 `window_focus` call paths
the title bar to red right after creation. The close icon
sits in the right-of-centre slot inside the title bar; click
handling lands in L141+.

## BIOS test status

Source-only as usual. Test confirms the header API,
`window_bring_to_top` body, the Makefile entry, and the
kernel init order + test-window creation. The build links.
