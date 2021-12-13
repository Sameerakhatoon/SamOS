# Lecture 100 - terminal part 4 (system terminal)

L100 retires the L1-L13 VGA-text terminal path inside
`kernel.c` and replaces it with a single graphics-backed
`system_terminal`. After this commit, every kernel `print` /
`panic` lands on the framebuffer.

## Code dropped

The legacy VGA implementation is gone:

- `uint16_t video_mem`, `terminal_row`, `terminal_col` globals.
- `terminal_make_char`, `terminal_putchar`,
  `vga_terminal_backspace`, `terminal_initialize`.
- The body of `terminal_writechar`, which used to write
  `terminal_make_char` cells into `0xB8000`.

These were already unreachable after the L74 UEFI pivot (no
BIOS text mode), but the symbols stayed in place to keep the
diff cosmetic. L100 deletes them.

## Code added

- `struct terminal* system_terminal` - module global the
  forwarder reads.
- `terminal_writechar(c, colour)` becomes a five-liner: if
  `system_terminal` is NULL, return; otherwise call
  `terminal_write(system_terminal, c)`. The `colour` argument
  is preserved for ABI compatibility but ignored - the graphics
  terminal carries its own colour.
- In `kernel_main`, between `font_system_init` and the existing
  flow, the system terminal gets built:

  ```c
  terminal_system_setup();
  struct font* system_font_local = font_get_system_font();
  if(!system_font_local){
      panic("Failed to load system font\n");
  }
  struct framebuffer_pixel font_color = {0};
  font_color.red = 0xff;
  system_terminal = terminal_create(screen_info, 0, 0,
                                    screen_info->width,
                                    screen_info->height,
                                    system_font_local, font_color,
                                    TERMINAL_FLAG_BACKSPACE_ALLOWED);
  ```

The terminal covers the full screen surface, allows backspace,
and writes in red. `screen_info` is captured just after
`graphics_setup` so the bounds are correct on UEFI-resolved
framebuffers.

## Pre-graphics print

Some early `print` calls run BEFORE `graphics_setup`. With the
NULL-guard in `terminal_writechar`, those messages are silently
swallowed; there is no console to receive them. Upstream
accepts the same behaviour. Once we add a UEFI-side log buffer
those messages can be replayed.

## L95 Hello-world removed

The L95 standalone `font_draw_text` smoke is gone too; the
system_terminal owns the screen now and any output goes through
it via `print`.

## BIOS test status

Source-only. Test verifies the legacy symbols are deleted, the
new `system_terminal` handle exists, `terminal_writechar`
forwards, the boot sequence creates the terminal with the
correct flag, the header include is present, and the build
links.
