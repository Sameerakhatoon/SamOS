#include "classic.h"
#include "keyboard.h"
#include "io/isa.h"         // Ports and IRQ line
#include "kernel.h"
#include "arch/arch.h"       // arch_irq_register / _unmask
#include "task/task.h"
#include <stdint.h>
#include <stddef.h>

int     classic_keyboard_init();
void    classic_keyboard_handle_interrupt();
uint8_t classic_keyboard_scancode_to_char(uint8_t scancode);

static uint8_t keyboard_scan_set_one[] = {
    0x00, 0x1B, '1',  '2',  '3',  '4',  '5',  '6',
    '7',  '8',  '9',  '0',  '-',  '=',  0x08, '\t',
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    'O',  'P',  '[',  ']',  0x0D, 0x00, 'A',  'S',
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ';',
    '\'', '`',  0x00, '\\', 'Z',  'X',  'C',  'V',
    'B',  'N',  'M',  ',',  '.',  '/',  0x00, '*',
    0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, '7',
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.'
};

// US layout, shift held. Same indices as the table above;
// letters are handled by the caps/shift rule, so they repeat here
// unchanged. Keypad entries stay digits (Num Lock assumed on).
static uint8_t keyboard_scan_set_one_shifted[] = {
    0x00, 0x1B, '!',  '@',  '#',  '$',  '%',  '^',
    '&',  '*',  '(',  ')',  '_',  '+',  0x08, '\t',
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    'O',  'P',  '{',  '}',  0x0D, 0x00, 'A',  'S',
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
    '"',  '~',  0x00, '|',  'Z',  'X',  'C',  'V',
    'B',  'N',  'M',  '<',  '>',  '?',  0x00, '*',
    0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, '7',
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.'
};

// E0-prefixed navigation keys become VT100/xterm escape
// sequences, so userland sees ordinary bytes through getkey and a
// terminal program can parse them the usual way.
struct classic_extended_key {
    uint8_t     scancode;
    const char* sequence;
};

static const struct classic_extended_key keyboard_extended_keys[] = {
    {0x48, "\x1b[A"},   // Up
    {0x50, "\x1b[B"},   // Down
    {0x4D, "\x1b[C"},   // Right
    {0x4B, "\x1b[D"},   // Left
    {0x47, "\x1b[H"},   // Home
    {0x4F, "\x1b[F"},   // End
    {0x52, "\x1b[2~"},  // Insert
    {0x53, "\x1b[3~"},  // Delete
    {0x49, "\x1b[5~"},  // Page Up
    {0x51, "\x1b[6~"},  // Page Down
    {0x1C, "\r"},       // Keypad Enter
    {0x35, "/"},        // Keypad /
};

struct keyboard classic_keyboard = {
    .name = "Classic",
    .init = classic_keyboard_init
};

// Driver state machine. Only the IRQ handler (or the selftest,
// with interrupts in a known state) touches these.
static int classic_e0_pending   = 0;
static int classic_e1_remaining = 0;

int classic_keyboard_init(){
    arch_irq_register(IRQ_KEYBOARD, classic_keyboard_handle_interrupt);
    keyboard_set_capslock(&classic_keyboard, KEYBOARD_CAPS_LOCK_OFF);
    classic_keyboard_reset_state();

    // Lecture 68 - unmask IRQ1 (keyboard) at the interrupt
    // controller. After this scancode interrupts reach the handler
    // registered above.
    arch_irq_unmask(IRQ_KEYBOARD);

    isa_write8(PS2_PORT, PS2_COMMAND_ENABLE_FIRST_PORT);
    return 0;
}

void classic_keyboard_reset_state(void){
    classic_e0_pending   = 0;
    classic_e1_remaining = 0;
    classic_keyboard.modifiers = 0;
}

static int classic_modifier_held(int mask){
    return (classic_keyboard.modifiers & mask) != 0;
}

static void classic_modifier_set(int mask, int pressed){
    if(pressed){
        classic_keyboard.modifiers |= mask;
    } else {
        classic_keyboard.modifiers &= ~mask;
    }
}

uint8_t classic_keyboard_scancode_to_char(uint8_t scancode){
    size_t size_of_keyboard_set_one = sizeof(keyboard_scan_set_one) / sizeof(uint8_t);
    // Was `>`, which let scancode == size read one byte
    // past the end of the table.
    if(scancode >= size_of_keyboard_set_one){
        return 0;
    }
    int shift = classic_modifier_held(KEYBOARD_MOD_SHIFT);
    char c = shift ? keyboard_scan_set_one_shifted[scancode]
                   : keyboard_scan_set_one[scancode];
    if(c >= 'A' && c <= 'Z'){
        // Caps Lock and Shift cancel each other out on letters.
        int upper = shift ^ (keyboard_get_capslock(&classic_keyboard) == KEYBOARD_CAPS_LOCK_ON);
        if(!upper){
            c += 32;
        }
    }
    return c;
}

static int classic_copy_sequence(const char* sequence, char* out){
    int n = 0;
    while(sequence[n] && n < CLASSIC_KEYBOARD_MAX_SEQUENCE){
        out[n] = sequence[n];
        n++;
    }
    return n;
}

int classic_keyboard_translate(uint8_t scancode, char* out){
    // Pause/Break sends E1 1D 45 E1 9D C5: swallow the tail.
    if(classic_e1_remaining > 0){
        classic_e1_remaining--;
        return 0;
    }
    if(scancode == CLASSIC_KEYBOARD_E1_PREFIX){
        classic_e1_remaining = 5;
        return 0;
    }
    if(scancode == CLASSIC_KEYBOARD_E0_PREFIX){
        classic_e0_pending = 1;
        return 0;
    }

    int extended = classic_e0_pending;
    classic_e0_pending = 0;
    int released = (scancode & CLASSIC_KEYBOARD_KEY_RELEASED) != 0;
    uint8_t code = scancode & ~CLASSIC_KEYBOARD_KEY_RELEASED;

    // Modifiers update on both press and release.
    switch(code){
        case CLASSIC_KEYBOARD_LSHIFT:
        case CLASSIC_KEYBOARD_RSHIFT:
            // E0 2A / E0 36 are "fake shifts" some keyboards wrap
            // around navigation keys; they are not real presses.
            if(!extended){
                classic_modifier_set(code == CLASSIC_KEYBOARD_LSHIFT
                                         ? KEYBOARD_MOD_LSHIFT : KEYBOARD_MOD_RSHIFT,
                                     !released);
            }
            return 0;
        case CLASSIC_KEYBOARD_CTRL:
            classic_modifier_set(extended ? KEYBOARD_MOD_RCTRL : KEYBOARD_MOD_LCTRL, !released);
            return 0;
        case CLASSIC_KEYBOARD_ALT:
            classic_modifier_set(extended ? KEYBOARD_MOD_ALTGR : KEYBOARD_MOD_ALT, !released);
            return 0;
    }

    if(released){
        return 0;
    }

    if(extended){
        for(size_t i = 0; i < sizeof(keyboard_extended_keys) / sizeof(keyboard_extended_keys[0]); i++){
            if(keyboard_extended_keys[i].scancode == code){
                return classic_copy_sequence(keyboard_extended_keys[i].sequence, out);
            }
        }
        return 0;
    }

    if(code == CLASSIC_KEYBOARD_CAPSLOCK){
        KEYBOARD_CAPS_LOCK_STATE old_state = keyboard_get_capslock(&classic_keyboard);
        keyboard_set_capslock(&classic_keyboard,
                              old_state == KEYBOARD_CAPS_LOCK_ON
                                  ? KEYBOARD_CAPS_LOCK_OFF
                                  : KEYBOARD_CAPS_LOCK_ON);
        return 0;
    }

    char c = (char)classic_keyboard_scancode_to_char(code);
    if(c == 0){
        return 0;
    }

    // Ctrl+letter / Ctrl+[\]^_ -> the C0 control code (Ctrl-C = 3).
    if(classic_modifier_held(KEYBOARD_MOD_CTRL)){
        if(c >= 'a' && c <= 'z'){
            c -= 'a' - 1;
        } else if(c >= '@' && c <= '_'){
            c -= '@';
        }
    }

    // US layout has no third level, so AltGr only marks state. Left
    // Alt follows the xterm "meta sends escape" convention.
    int n = 0;
    if(classic_modifier_held(KEYBOARD_MOD_ALT)){
        out[n++] = 0x1B;
    }
    out[n++] = c;
    return n;
}

void classic_keyboard_handle_interrupt(){
    // Read the data port exactly once per IRQ. The old code
    // read it twice, so the second read could swallow the next
    // byte of a multi-byte sequence. Paging is
    // handled by interrupt_handler; calling task_page() here when
    // no task exists dereferenced NULL.
    // An IRQ1 can be latched with no keyboard byte waiting (QEMU
    // raises it for the mouse handshake's config-byte reply), and
    // an aux byte belongs to the mouse: read 0x60 only for a key.
    uint8_t status = isa_read8(PS2_PORT);
    if(!(status & PS2_STATUS_OUTPUT_FULL) || (status & PS2_STATUS_AUX_DATA)){
        return;
    }
    uint8_t scancode = isa_read8(KEYBOARD_INPUT_PORT);

    KEYBOARD_CAPS_LOCK_STATE caps_before = keyboard_get_capslock(&classic_keyboard);
    char sequence[CLASSIC_KEYBOARD_MAX_SEQUENCE];
    int n = classic_keyboard_translate(scancode, sequence);
    for(int i = 0; i < n; i++){
        keyboard_push(sequence[i]);
    }

    if(keyboard_get_capslock(&classic_keyboard) != caps_before){
        struct keyboard_event event = {0};
        event.type = KEYBOARD_EVENT_CAPS_LOCK_CHANGE;
        event.data.caps_lock.state = keyboard_get_capslock(&classic_keyboard);
        keyboard_push_event_to_listeners(&classic_keyboard, &event);
    }
}

struct keyboard* classic_init(){
    return &classic_keyboard;
}
