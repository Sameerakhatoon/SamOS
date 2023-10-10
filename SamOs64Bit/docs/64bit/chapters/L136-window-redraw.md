# Lecture 136 - window_redraw + test the windows

L136 lands the real `window_redraw(window)` body and swaps the
L129 udelay smoke loop in `kernel_main` for a `window_create`
test.

## `window_redraw`

```c
void window_redraw(struct window* window){
    graphics_redraw(window->root_graphics);
}
```

One-liner: composite the window's root surface to the
framebuffer. L93's `graphics_redraw` walks children, so the
title bar, four borders, and body all flush in one call.

The L134 weak stub in `window.c` is removed.

## `window.h`

Adds the prototype.

## `kernel.c`

The L129 udelay loop is gone. In its place:

```c
struct window* win = window_create(graphics_screen_info(), NULL,
                                   "Test Window",
                                   100, 100, 200, 200, 0, -1);
if(!win){
    print("WIndow creation problem\n");
}
while(1){ }
```

Auto-id via `-1`. The print typo `"WIndow creation problem"`
is verbatim from upstream and preserved.

## Test under OVMF (manual)

After this commit the kernel should show one window at
(100, 100) with a red title bar and a white body. Calling
`window_position_set` would walk the L134/L135 chain and
window_redraw would flush.

## BIOS test status

Source + link. Test asserts the real window_redraw lives in
window.c, the weak stub is gone, the header has the decl, the
test-window literals are in kernel.c (including the typo), and
the build links.
