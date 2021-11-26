# Lecture 87 - graphics foundations

L87 lands the first kernel-side graphics code. The UEFI handoff
from L86 left the framebuffer base, the resolution, and the
pitch in `rdi/edx/ecx/esi` at the moment we entered the kernel.
L87 catches those values into a bss-resident `default_graphics_info`
struct in `kernel.asm` before any C runs, then teaches the C side
to draw pixels into a back buffer and composite them onto the
real framebuffer.

## Pieces

### `src/graphics/graphics.h`

`struct framebuffer_pixel` is `{ blue, green, red, reserved }` -
the natural memory order for the linear framebuffer.

`struct graphics_info` is the surface type. It owns:

- `framebuffer` and the (width, height, pixels_per_scanline,
  resolution) the framebuffer was created with.
- A `pixels` back buffer the kernel writes into. `redraw` copies
  this region into `framebuffer` at the surface's screen position.
- Position (`starting_x/y` for absolute, `relative_x/y` for
  parent-relative) and a `children` vector for the future
  compositor.
- `ignore_color` (skip these on `draw_pixel`) and
  `transparency_key` (skip these during compositing). Black is
  the sentinel for "no key".
- Mouse event handlers (stubbed; the mouse driver does not exist
  yet).

The public API is `graphics_setup`, `graphics_screen_info`,
`graphics_draw_pixel`, `graphics_redraw`, `graphics_redraw_all`.

### `src/graphics/graphics.c`

`graphics_setup` is one-shot. It:

1. Saves the physical framebuffer parameters into module-statics.
2. Allocates a kernel-side back buffer (`new_framebuffer_memory`)
   of `width * pixels_per_scanline * sizeof(pixel)`.
3. Allocates a `pixels` buffer of the same size.
4. `paging_map_to` aliases `new_framebuffer_memory` onto the
   physical framebuffer so writes through the buffer pointer hit
   the screen.
5. Zero-fills the back buffer through `draw_pixel` (the upstream
   pattern, exercises the ignore/key path early).
6. Pushes the root onto the `graphics_info_vector` registry.

`graphics_paste_pixels_to_framebuffer` (file-static) does the
actual blit. It clips the source rectangle against the source
surface AND clips the destination against the screen, then loops
per-row. Transparency-keyed pixels are skipped.

`graphics_redraw` calls `paste_pixels` at the surface's screen
position. `graphics_redraw_all` calls `redraw` on the root.
Children are not walked yet.

### `src/kernel.asm`

- `global default_graphics_info` exports the struct.
- Right after the long-mode GDT pivot, before any C runs:

```
mov [default_graphics_info + 0],  rdi   ; framebuffer
mov [default_graphics_info + 8],  edx   ; horizontal_resolution
mov [default_graphics_info + 12], ecx   ; vertical_resolution
mov [default_graphics_info + 16], esi   ; pixels_per_scanline
```

- At the bottom of the file the struct is declared with one `dq`
  / `dd` per C field. Offsets in the comments are pinned to the C
  struct order; any rearrangement on either side will need both
  edited together.

### `src/kernel.c`

- `#include "graphics/graphics.h"`.
- `extern struct graphics_info default_graphics_info;` so the C
  side can see the asm symbol.
- After `kheap_post_paging`:
  - `graphics_setup(&default_graphics_info)`.
  - Paint a 100x100 red square in the top-left corner of the
    screen surface.
  - `graphics_redraw_all`.

### `Makefile` + `build.sh`

`build/graphics/graphics.o` joins `FILES`. A new compile rule
adds `mkdir -p ./build/graphics` so a clean build does not race
the directory creation. `build.sh` adds `build/graphics` to its
upfront mkdir list.

## Layout sync between C and ASM

The asm `default_graphics_info` lays out the same fields the C
`struct graphics_info` does, in the same order, with explicit
padding to keep `pixels` at offset 24 (so the long-mode handoff
to offsets 0/8/12/16 lines up). Field comments in the asm pin
the contract. If the C struct grows or reorders, the asm must
follow.

The natural alternative (define the struct in C, refer to it
from asm via `extern`) does not work cleanly here: asm cannot
take a C struct's field offsets without help. We could autogen
the offsets header, but for now a single static struct + a
big comment is cheaper than the tooling.

## BIOS test status

The graphics code never runs under BIOS (default_graphics_info
stays zeroed and `graphics_setup` would map a zero-pointer
framebuffer). That is fine for source-checking: BIOS boot has
been broken since L74. The test verifies headers, API, asm
exports, the four register stash lines, the kernel.c hookup, and
the build artifacts.

When the UEFI runtime path is up the test becomes "boot with OVMF,
see green screen turn black with a red corner".
