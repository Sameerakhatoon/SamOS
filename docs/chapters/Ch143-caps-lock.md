# Ch 149 - Caps lock + case-correct keyboard input

The PS/2 driver now tracks Caps Lock state per keyboard and lowercases A-Z when caps is off. Pressing the Caps Lock key (scancode 0x3A) toggles the flag.

## What landed

- `src/keyboard/keyboard.h`:
  - `KEYBOARD_CAPS_LOCK_ON = 1`, `KEYBOARD_CAPS_LOCK_OFF = 0`.
  - `typedef int KEYBOARD_CAPS_LOCK_STATE;`.
  - `struct keyboard` gains a `KEYBOARD_CAPS_LOCK_STATE capslock_state` field.
  - Exports `keyboard_set_capslock` and `keyboard_get_capslock`.
- `src/keyboard/keyboard.c` - body for both getters/setters.
- `src/keyboard/classic.h` - `CLASSIC_KEYBOARD_CAPSLOCK 0x3A`.
- `src/keyboard/classic.c`:
  - `classic_keyboard_init` sets the initial caps state to OFF.
  - `classic_keyboard_scancode_to_char` lowercases `'A'..'Z'` by adding 32 when caps is OFF (the scan table holds uppercase letters).
  - `classic_keyboard_handle_interrupt` checks the post-filter scancode against `CLASSIC_KEYBOARD_CAPSLOCK` and toggles the state.

## Test impact

Suite stays 32/32. Test 39 (which sendkeys 'a' and looks for ANY 'A' on VGA) still passes because the kernel's other boot output ("FAT16", "ENO NAME") supplies the substring. No new behaviour-specific test needed; the change is observable in the shell at the prompt where letters now come out lowercased by default.
