# Lecture 88 part 1 - image abstraction

L88 splits across two commits. Part 1 introduces a generic image
registry; part 2 plugs the BMP decoder into it. This commit lands
no concrete decoder; the registry is empty and
`graphics_image_load` will always fail with `-EINFORMAT` until
part 2.

## Pieces

`src/graphics/image/image.h` declares:

- `union image_pixel_data` (RGBA, accessible as one `uint32_t`
  or four `uint8_t` fields).
- `struct image` (width, height, row-major `data`, `private`,
  back-pointer to the `image_format` that loaded it).
- `struct image_format` (MIME string, load/free callbacks,
  register/unregister hooks, private slot).
- The public registry API.

`src/graphics/image/image.c` implements the registry. The init
function builds a vector of `struct image_format*` and calls
`graphics_image_formats_load`, which is the L88p2 hook. The
load path walks every registered format and uses the first
whose `image_load_function` returns non-NULL.

## Upstream bugs preserved

Two notable things about the upstream commit `fb6127f`:

1. `graphics_image_load` is missing the `return img` at the end.
   Calling it would return whatever happened to be in `%rax`,
   which works by accident if optimisation keeps `img` there.
   We add the explicit `return img` so the code is correct under
   `-O0`.

2. The unload helper is spelled `grpahics_image_format_unload` -
   the `r` and `a` are swapped. Upstream calls this misspelled
   name from `graphics_image_format_register` and from the
   `unload` loop. We keep the typo (per project policy: preserve
   identifier typos so the diff against upstream stays clean)
   and add a `graphics_image_format_unload` alias that forwards
   to it, so call sites can use either spelling.

`graphics_image_load` does its own `fclose` and `kfree(img_memory)`
in the cleanup block. The L88 docs note that on success the
pixel data has been copied into the format-private storage by
the decoder (see L88p2), so freeing `img_memory` is safe.

## Build wiring

- `Makefile`: `build/graphics/image/image.o` joins `FILES`, with
  a `mkdir -p ./build/graphics/image` in the recipe to survive a
  fresh checkout.
- `build.sh`: `build/graphics/image` joins the upfront mkdir.

No call site lights up yet. The registry stays empty;
`graphics_image_formats_load` returns 0 immediately. `kernel_main`
will start calling `graphics_image_formats_init` after part 2.

## BIOS test status

Source-only as usual since L74. The test verifies the header,
the .c body, the public API, the Makefile and build.sh entries,
and that the build still links.
