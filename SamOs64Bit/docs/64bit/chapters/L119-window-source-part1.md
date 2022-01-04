# Lecture 119 - window source part 1 (source-only)

L119 starts `src/graphics/window.c`. Source-only at this
lecture; `window.o` is not in `FILES`. Upstream follows the
same pattern: the file is incomplete (the `window_create`
function falls off the end without returning) and references
helpers L120-L123 add.

## Module-globals

- `struct vector* windows_vector` - every live window
  registers here.
- `struct image* close_icon` - loaded at boot from
  `@:/clsicon.bmp`. Painted in the title bar.
- `struct window* window_moving` - drag-state.
- `struct window* focused_window` - input focus.
- `int window_autoincrement_id_current = 100000` - id stamped
  on windows created with `id == -1`.

## `window_system_initialize`

1. Allocate `windows_vector` (sizeof(struct window*), cap 10).
2. `graphics_image_load("@:/clsicon.bmp")` into `close_icon`.
3. Reset `window_moving`, `focused_window`,
   `window_autoincrement_id_current`.

Returns `-ENOMEM` on vector failure, `-EIO` on missing close
icon.

## `window_system_initialize_stage2`

Hook for mouse + keyboard listener registration. Stub today;
L137+ and L175 fill in the wiring.

## `window_create` (part 1)

Validates inputs, picks the system font when `font == NULL`,
allocates `struct window`, stamps the auto-incremented id when
the caller passed `-1`, copies title and bounds.

Builds the root graphics_info via
`graphics_info_create_relative` (L116). When
`WINDOW_FLAG_BORDERLESS` is clear, the bounds expand by
`2 * WINDOW_BORDER_PIXEL_SIZE` horizontally and
`WINDOW_TITLE_BAR_HEIGHT + WINDOW_BORDER_PIXEL_SIZE` vertically
so the body still ends up `width x height` of usable pixels.

When `WINDOW_FLAG_BACKGROUND_TRANSPARENT` is set, white
`(R=G=B=0xff)` becomes the transparency key and the root
surface is filled with it (so the body is see-through by
default).

The function ends here. L120 appends the title-bar / borders /
return.

## Upstream bug preserved

`window_create` is `struct window* (...)` but never executes a
`return` statement. The behaviour is undefined; the upstream
binary works only because the function is not callable yet
(window.o is not linked). We keep the shape so the per-commit
diff stays linear.

## `config.h`

```
#define WINDOW_BORDER_PIXEL_SIZE 2
#define WINDOW_TITLE_BAR_HEIGHT  32
```

## BIOS test status

Source-only. Test asserts the file exists, every L119 symbol
is present, the `clsicon.bmp` load is wired, window.o is NOT in
FILES, and the build still links.
