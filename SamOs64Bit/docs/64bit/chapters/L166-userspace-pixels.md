# Lecture 166 - userspace pixels

Upstream: PeachOS64BitCourse c4a698b.

## What landed

- `paging_align_value_to_upper_page` rounds an arbitrary value
  up to the next page boundary, used by the pixel-buffer mapper.
- `process_map_into_userspace` maps a kernel physical range
  into the process address space. Phys ends must be page
  aligned. SamOs's `paging_map_range` takes a page count rather
  than a byte count, so we divide here. Documented as a port
  delta; upstream passes bytes.
- `process_map_graphics_framebuffer_pixels_into_userspace`
  computes the pixel-buffer byte size, rounds up, and forwards
  to `process_map_into_userspace` with PAGING_ACCESS_FROM_ALL |
  CACHE_DISABLED | PRESENT | WRITEABLE.
- `SYSTEM_COMMAND20_GRAPHICS_PIXELS_BUFFER_GET` /
  `isr80h_command20_graphics_pixels_get` resolves a userland
  graphics handle, walks the sentinel back to the kernel
  graphics_info, and hands the mapped userspace pointer back.

## BIOS test status

Source + link. Build links.
