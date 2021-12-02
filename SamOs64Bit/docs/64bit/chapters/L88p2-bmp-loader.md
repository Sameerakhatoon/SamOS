# Lecture 88 part 2 - BMP loader

Part 2 plugs a 24-bit uncompressed BMP decoder into the L88p1
registry. After this commit `graphics_image_load("@:/FOO.BMP")`
will return a populated `struct image` for any BMP that fits the
narrow profile we support.

## Format coverage

- BITMAPFILEHEADER + BITMAPINFOHEADER (Windows v3 layout).
- 24 bits per pixel only. 1/4/8/16 bpp and 32 bpp are rejected
  with `-EUNIMP`; the registry will move on to the next format
  (none, in this commit).
- Uncompressed only (`BI_RGB`). RLE4/RLE8 and BITFIELDS get the
  same `-EUNIMP` treatment.
- Top-down or bottom-up. Bottom-up is the BMP default; the row
  index is flipped while decoding so the output `image->data`
  is always top-down in memory.

## Decode walk

1. Validate `size >= sizeof(bmp_header)`.
2. Match the "BM" signature in `bf_type`.
3. Confirm `bf_offbits < size` (else the pixel data starts past
   end of file).
4. Reject non-uncompressed or non-24bpp.
5. Allocate `width * height * sizeof(image_pixel_data)` for the
   output buffer.
6. Padded row size is `(width * 3 + 3) & ~3`. Walk every row,
   read three bytes per pixel as B/G/R, write them into the
   destination index `dest_row * width + col` where `dest_row`
   is flipped for bottom-up.

The BMP file is read into a temporary buffer by the L88p1
loader; this routine only walks that buffer and produces the
decoded RGBA. The temporary buffer is freed by the L88p1 code,
not by us.

## Wiring

- `Makefile`: `build/graphics/image/bmp.o` joins `FILES`, with
  its own recipe.
- `image.c::graphics_image_formats_load` now calls
  `graphics_image_format_register(graphics_image_format_bmp_setup())`.
- `graphics.c::graphics_setup` calls
  `graphics_image_formats_init` once the screen is up, so
  `kernel_main` does not need to remember to do it.

## Errors

- `-EINFORMAT`: header too small, signature wrong, offset out
  of range, padded row data overruns the file, or dimensions
  non-positive.
- `-ENOMEM`: image header or pixel buffer allocation failed.
- `-EUNIMP`: compression non-zero or bpp != 24.

On any error the partial allocation is unwound and `NULL` is
returned, signalling the registry to try the next format.

## BIOS test status

Source-only as usual. The test checks the headers, key
identifiers in the .c, the registry hookup in image.c, the
formats_init call in graphics.c, and the Makefile entry, then
re-runs the build.
