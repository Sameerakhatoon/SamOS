// Host-side test of the PS/2 scancode-set-1 translator in
// src/keyboard/classic.c: Shift, Caps Lock, Ctrl, Alt, AltGr, the
// E0 prefix and the table bounds. run.sh links classic.c with the
// stubs below (the arch/arch.h calls it makes).
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "keyboard/keyboard.h"
#include "keyboard/classic.h"
#include "arch/arch.h"

uint8_t classic_keyboard_scancode_to_char(uint8_t scancode);

// ---- stubs for what classic.c calls ----------------------------
void arch_pio_write8(uint16_t port, uint8_t val){ (void)port; (void)val; }
uint8_t arch_pio_read8(uint16_t port){ (void)port; return 0; }
int arch_irq_register(int irq, INTERRUPT_CALLBACK_FUNCTION cb){ (void)irq; (void)cb; return 0; }
void arch_irq_unmask(int irq){ (void)irq; }
void keyboard_push(char c){ (void)c; }
void keyboard_push_event_to_listeners(struct keyboard* k, struct keyboard_event* e){ (void)k; (void)e; }
void keyboard_set_capslock(struct keyboard* k, KEYBOARD_CAPS_LOCK_STATE s){ k->capslock_state = s; }
KEYBOARD_CAPS_LOCK_STATE keyboard_get_capslock(struct keyboard* k){ return k->capslock_state; }

static int failures, checks;

// Feed a scancode sequence, collect everything it produced.
static void feed(const uint8_t* codes, int n, char* out){
    int len = 0;
    for(int i = 0; i < n; i++){
        char seq[CLASSIC_KEYBOARD_MAX_SEQUENCE];
        int k = classic_keyboard_translate(codes[i], seq);
        memcpy(out + len, seq, k);
        len += k;
    }
    out[len] = 0;
}

#define EXPECT(want, ...) do { const uint8_t c_[] = {__VA_ARGS__}; char o_[64]; \
    feed(c_, sizeof c_, o_); checks++; \
    if(strcmp(o_, want) != 0){ failures++; printf("FAIL line %d: got \"", __LINE__); \
        for(char* p_ = o_; *p_; p_++) printf((unsigned char)*p_ < 32 ? "\\x%02x" : "%c", *p_); \
        printf("\"\n"); } } while(0)

#define LSHIFT 0x2A
#define LSHIFT_UP 0xAA
#define RSHIFT 0x36
#define RSHIFT_UP 0xB6

int main(void){
    struct keyboard* kbd = classic_init();
    kbd->init();

    EXPECT("a", 0x1E, 0x9E);                                 // a press/release
    EXPECT("A", LSHIFT, 0x1E, 0x9E, LSHIFT_UP);              // Shift+a
    EXPECT("a", 0x1E);                                       // shift released
    EXPECT("!@#$%^&*()_+", LSHIFT, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
           0x0A, 0x0B, 0x0C, 0x0D, LSHIFT_UP);
    EXPECT(":\"?<>{}|~", RSHIFT, 0x27, 0x28, 0x35, 0x33, 0x34, 0x1A, 0x1B, 0x2B, 0x29, RSHIFT_UP);
    EXPECT(";'/,.[]\\`-=", 0x27, 0x28, 0x35, 0x33, 0x34, 0x1A, 0x1B, 0x2B, 0x29, 0x0C, 0x0D);

    // Caps Lock: letters only, and Shift inverts it.
    EXPECT("A1a", 0x3A, 0xBA, 0x1E, 0x02, LSHIFT, 0x1E, LSHIFT_UP);
    EXPECT("a", 0x3A, 0xBA, 0x1E);                           // caps off again

    // Ctrl+C -> 0x03, right Ctrl (E0 1D) too.
    EXPECT("\x03", 0x1D, 0x2E, 0x9D);
    EXPECT("\x03", 0xE0, 0x1D, 0x2E, 0xE0, 0x9D);
    EXPECT("c", 0x2E);                                       // ctrl released

    // Alt+x -> ESC x; AltGr (E0 38) is tracked but adds nothing on US.
    EXPECT("\x1bx", 0x38, 0x2D, 0xB8);
    EXPECT("x", 0xE0, 0x38, 0x2D, 0xE0, 0xB8);
    checks++;
    if(kbd->modifiers != 0){ failures++; printf("FAIL modifiers left set: %x\n", kbd->modifiers); }

    // E0 navigation keys -> escape sequences; releases produce nothing.
    EXPECT("\x1b[A", 0xE0, 0x48, 0xE0, 0xC8);
    EXPECT("\x1b[B\x1b[C\x1b[D", 0xE0, 0x50, 0xE0, 0x4D, 0xE0, 0x4B);
    EXPECT("\x1b[H\x1b[F\x1b[3~", 0xE0, 0x47, 0xE0, 0x4F, 0xE0, 0x53);
    // Without E0 the same codes are keypad digits.
    EXPECT("8", 0x48);
    // Fake shift around an E0 key does not stick.
    EXPECT("\x1b[Aa", 0xE0, 0x2A, 0xE0, 0x48, 0xE0, 0xC8, 0xE0, 0xAA, 0x1E);
    // Keypad Enter / slash.
    EXPECT("\r/", 0xE0, 0x1C, 0xE0, 0x35);
    // Pause (E1 1D 45 E1 9D C5) is swallowed whole.
    EXPECT("b", 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5, 0x30);

    // Bounds: 0x54 is one past the table (the old `>` check let
    // exactly this index through), 0x57 = F11 is unmapped.
    EXPECT("", 0x54, 0x57, 0x7F);
    checks++;
    if(classic_keyboard_scancode_to_char(0x54) != 0){ failures++; printf("FAIL table bound\n"); }

    printf("keyboard host test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
