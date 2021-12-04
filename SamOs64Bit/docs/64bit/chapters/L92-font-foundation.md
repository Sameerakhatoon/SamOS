# Lecture 92 - font system part 1

L92 adds the font module as source-only files. Upstream
deliberately leaves `font.c` out of `FILES`; the code references
two functions that do not exist yet (`font_load` and
`graphics_redraw_graphics_to_screen`) and `font_load_from_image`
falls off the end without producing a `struct font`. Trying to
link it at this lecture would fail.

We follow the same pattern: ship the file for reference, no
Makefile update.

## `struct font`

A fixed-pitch bitmap font. `character_data` is a packed bitmap
(one bit per source pixel) with one slab per character. The
slab size is computed from
`bits_width_per_character * bits_height_per_chracter` rounded
up to a whole number of bytes. The intended layout is
`bit_index = y * width + x`, byte `bit_index / 8`, mask
`1 << (bit_index % 8)`.

`subtract_from_ascii_char_index_for_drawing` is the offset
applied to incoming ASCII before indexing into
`character_data`. The system font starts at ASCII 32 (space),
so the value will be 32 for the system font - characters below
space (control codes) are not drawn.

Identifier typos preserved:

- `bits_height_per_chracter` (missing the second `a` in
  "character").
- `font_load_from_image` upstream just falls off the end without
  a `return`. We add a `(void)character_data; return NULL;` so
  the unused-variable warning does not fire under `-Werror`. The
  L94 commit will replace the body with the real
  `font_create` + `return font` path.

## Loader walk (for reference)

`font_load_from_image` opens the font BMP via
`graphics_image_load`, then for every character cell in the
image:

```
for(row = 0; row < total_rows; row++)
  for(col = 0; col < characters_per_row; col++)
    starting_x = col * pixel_width
    starting_y = row * pixel_height
    for(x = 0; x < pixel_width; x++)
      for(y = y_offset_per_character; y < pixel_height; y++)
        if(any RGB component of the source pixel != 0)
          set the (x, y) bit of the character's slab
```

The `y_offset_per_character` parameter skips a per-character
header strip; the upstream system font reserves the top
`FONT_IMAGE_CHARACTER_Y_OFFSET` rows for metadata and the actual
glyph starts below it.

## What does not work yet

- `font_load_from_image` does not return the assembled `struct
  font`. L94 fixes this.
- `font_system_init` calls `font_load`, which is not defined.
- `font_draw_from_index` calls
  `graphics_redraw_graphics_to_screen`, which does not exist.
  L93 introduces a regional redraw and L95 wires it in.
- The Makefile still does not list `font.o`.

So this lecture's contribution is purely the type and the
documented walk - good for following the upstream slope without
breaking the build.

## BIOS test status

Source-only. The test confirms both files exist, key
identifiers and macros are present, the typo is preserved, and
`font.o` is NOT yet in `FILES` (a positive correctness check
because adding it now would break the link).
