#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "lib/vector/vector.h"

#define KEYBOARD_CAPS_LOCK_ON   1
#define KEYBOARD_CAPS_LOCK_OFF  0

typedef int KEYBOARD_CAPS_LOCK_STATE;

// Modifier bits kept in struct keyboard.modifiers.
enum {
    KEYBOARD_MOD_LSHIFT = 0b00000001,
    KEYBOARD_MOD_RSHIFT = 0b00000010,
    KEYBOARD_MOD_LCTRL  = 0b00000100,
    KEYBOARD_MOD_RCTRL  = 0b00001000,
    KEYBOARD_MOD_ALT    = 0b00010000,
    KEYBOARD_MOD_ALTGR  = 0b00100000,

    KEYBOARD_MOD_SHIFT  = KEYBOARD_MOD_LSHIFT | KEYBOARD_MOD_RSHIFT,
    KEYBOARD_MOD_CTRL   = KEYBOARD_MOD_LCTRL  | KEYBOARD_MOD_RCTRL,
};

// Lecture 174 - keyboard event taxonomy.
typedef int KEYBOARD_EVENT_TYPE;
enum {
    KEYBOARD_EVENT_INVALID,
    KEYBOARD_EVENT_KEY_PRESS,
    KEYBOARD_EVENT_CAPS_LOCK_CHANGE,
};

struct keyboard;

struct keyboard_event {
    KEYBOARD_EVENT_TYPE type;
    union {
        struct {
            int key;
        } key_press;
        struct {
            KEYBOARD_CAPS_LOCK_STATE state;
        } caps_lock;
    } data;
    struct keyboard* keyboard;
};

typedef void (*KEYBOARD_EVENT_LISTENER_ON_EVENT)(struct keyboard* keyboard,
                                                 struct keyboard_event* event);

struct keyboard_listener {
    KEYBOARD_EVENT_LISTENER_ON_EVENT on_event;
};

struct process;

typedef int (*KEYBOARD_INIT_FUNCTION)();

struct keyboard {
    KEYBOARD_INIT_FUNCTION    init;
    char                      name[20];
    KEYBOARD_CAPS_LOCK_STATE  capslock_state;
    int                       modifiers;     // KEYBOARD_MOD_* bits

    // L174 - vector<struct keyboard_listener*>. Per-keyboard
    // listener registry. Populated when the keyboard is
    // inserted.
    struct vector*            key_listeners;

    struct keyboard*          next;
};

void keyboard_init();
int  keyboard_insert(struct keyboard* keyboard);
void keyboard_backspace(struct process* process);
void keyboard_push(char c);
char keyboard_pop();
void keyboard_set_capslock(struct keyboard* keyboard, KEYBOARD_CAPS_LOCK_STATE state);
KEYBOARD_CAPS_LOCK_STATE keyboard_get_capslock(struct keyboard* keyboard);
int  keyboard_get_modifiers(struct keyboard* keyboard);

// Keys dropped because the receiving process's ring was
// full. Monotonic; used by the selftest.
unsigned int keyboard_dropped_count(void);

// Lecture 174 - listener registry API.
struct keyboard*           keyboard_default();
int                        keyboard_register_handler(struct keyboard* keyboard,
                                                     struct keyboard_listener keyboard_listener);
int                        keyboard_unregister_handler(struct keyboard* keyboard,
                                                       struct keyboard_listener keyboard_listener);
struct keyboard_listener*  keyboard_get_listener_ptr(struct keyboard* keyboard,
                                                     struct keyboard_listener keyboard_listener);
void                       keyboard_push_event_to_listeners(struct keyboard* keyboard,
                                                            struct keyboard_event* event);

#endif
