#ifndef CLASSIC_KEYBOARD_H
#define CLASSIC_KEYBOARD_H

#include <stdint.h>

#define PS2_PORT                         0x64
#define PS2_COMMAND_ENABLE_FIRST_PORT    0xAE
#define PS2_STATUS_OUTPUT_FULL           0x01   // status bit 0: a byte waits at 0x60
#define PS2_STATUS_AUX_DATA              0x20   // status bit 5: that byte is from the mouse
#define CLASSIC_KEYBOARD_KEY_RELEASED    0x80
#define ISR_KEYBOARD_INTERRUPT           0x21
#define KEYBOARD_INPUT_PORT              0x60
#define CLASSIC_KEYBOARD_CAPSLOCK        0x3A

// Scancode set 1 modifier and prefix bytes.
#define CLASSIC_KEYBOARD_E0_PREFIX       0xE0
#define CLASSIC_KEYBOARD_E1_PREFIX       0xE1
#define CLASSIC_KEYBOARD_LSHIFT          0x2A
#define CLASSIC_KEYBOARD_RSHIFT          0x36
#define CLASSIC_KEYBOARD_CTRL            0x1D   // E0 1D = right Ctrl
#define CLASSIC_KEYBOARD_ALT             0x38   // E0 38 = right Alt (AltGr)

// The longest byte sequence one scancode can produce
// (Delete -> ESC [ 3 ~).
#define CLASSIC_KEYBOARD_MAX_SEQUENCE    4

struct keyboard;
struct keyboard* classic_init();

// Feed one raw scancode byte through the driver's state
// machine (modifiers, E0 prefix, caps lock) without touching the
// hardware. Writes the bytes the key produces into `out` (at most
// CLASSIC_KEYBOARD_MAX_SEQUENCE) and returns how many it wrote.
// The IRQ handler and the selftest both go through here.
int classic_keyboard_translate(uint8_t scancode, char* out);

// Reset modifier / prefix state (selftest helper).
void classic_keyboard_reset_state(void);

#endif
