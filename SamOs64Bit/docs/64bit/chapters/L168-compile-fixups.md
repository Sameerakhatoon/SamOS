# Lecture 168 - compiling our changes

Upstream: PeachOS64BitCourse 3f70686.

A pure cleanup pass to make L165-L167 actually compile in
upstream's tree:

- `isr80h/graphics.h` gains forward decls for `struct process`,
  `struct graphics_info`, and `struct interrupt_frame`.
- `task/process.h` hoists `struct framebuffer_pixel;` to the top
  of the file rather than carrying it next to the L166 helper
  decls.
- Upstream Makefile adds `isr80h/graphics.o` to FILES + recipe.
  SamOs landed both at L165, so no Makefile change here.

## BIOS test status

Source + link. Build links.
