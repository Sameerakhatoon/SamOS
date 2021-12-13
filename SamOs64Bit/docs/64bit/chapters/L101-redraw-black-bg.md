# Lecture 101 - redraw background as black, test the terminal

L101 is one line of code: add `graphics_redraw_all()` at the end
of `graphics_setup`. Visible effect: the L86 green
post-`gBS->ExitBootServices` sanity paint is replaced with the
back buffer (zeroed by the L87 init loop, i.e. black) the
moment the kernel sets up graphics. From there the system
terminal can write text against a clean background.

## Why this is the right place

`graphics_setup` already:

1. Allocates the back buffer.
2. Aliases it onto the physical framebuffer via paging_map_to.
3. Zero-fills the back buffer through `draw_pixel`.
4. Registers image formats.

Adding `graphics_redraw_all()` as the last step turns the zero
fill into a real screen clear. Any caller that runs after
`graphics_setup` can assume the screen is black.

## What changes for the user

- L86 painted the screen green just before
  `gBS->ExitBootServices`. That green stayed visible all the
  way through long-mode entry, paging setup, heap init, and
  disk discovery.
- After L101, the moment `kernel_main` reaches `graphics_setup`
  (right after `kheap_post_paging`), the green is replaced by
  black.
- Then `font_system_init` + `terminal_system_setup` +
  `terminal_create` build the system terminal, and the next
  `print` paints onto a clean canvas.

## What "test the terminal" means in upstream

Upstream's commit message says "and testing the terminal". The
commit itself only adds the one redraw line; the testing is
manual - launch the kernel under OVMF and check the terminal
behaves. We do source + build verification only.

## BIOS test status

Source-only. Test verifies the redraw call lives inside
`graphics_setup`'s body and the build links.
