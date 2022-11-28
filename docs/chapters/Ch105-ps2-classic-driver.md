# Ch 111 - PS/2 classic keyboard driver (part 1)

First real keyboard driver. Skeleton only - it registers itself, enables the PS/2 first port, defines the scan-code-set-1 lookup table, and provides a stub interrupt handler. IRQ1 -> driver wiring lands in Ch 113 once the IDT abstraction in Ch 112 is in place.

## What landed

- `src/keyboard/classic.h` exposes `PS2_PORT 0x64`, `PS2_COMMAND_ENABLE_FIRST_PORT 0xAE`, and `classic_init()`.
- `src/keyboard/classic.c` defines `keyboard_scan_set_one[]` (PS/2 scan code set 1 -> ASCII, indices 0x00..0x53), `struct keyboard classic_keyboard = { .name = "Classic", .init = classic_keyboard_init }`, plus `classic_keyboard_init` (writes `0xAE` to port `0x64`), `classic_keyboard_scancode_to_char`, an empty `classic_keyboard_handle_interrupt`, and `classic_init` returning the struct address.
- `src/keyboard/keyboard.c`'s `keyboard_init` now does `keyboard_insert(classic_init())`, which appends Classic to the driver chain and calls its init - meaning the PS/2 first port is enabled at boot.

## Scan code table note

The book ships the table OCR-mangled in print and points readers at the PeachOS commit. We use the canonical PS/2 set 1 mapping (US layout, uppercase letters per the book). Indices match the standard: 0x02='1', 0x10='Q', 0x1E='A', 0x2C='Z', 0x39=space, etc.

## Files touched

- `src/keyboard/classic.{h,c}` - new
- `src/keyboard/keyboard.c` - include + insert call
- `Makefile` - new FILES entry + build target
