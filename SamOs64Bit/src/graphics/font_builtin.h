#ifndef KERNEL_FONT_BUILTIN_H
#define KERNEL_FONT_BUILTIN_H
// Compiled-in fallback for the system font. Same glyphs as
// assets/sysfont.bmp; both come from tools/gen-sysfont.py.

#include <stdint.h>

#define FONT_BUILTIN_WIDTH            8
#define FONT_BUILTIN_HEIGHT           16
#define FONT_BUILTIN_FIRST_CHARACTER  32
#define FONT_BUILTIN_CHARACTER_COUNT  96

extern const uint8_t font_builtin_8x16_data[FONT_BUILTIN_CHARACTER_COUNT * FONT_BUILTIN_HEIGHT];

#endif
