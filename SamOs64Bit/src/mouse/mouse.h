#ifndef KERNEL_MOUSE_H
#define KERNEL_MOUSE_H
// Lecture 132 - mouse abstraction. A `struct mouse` is a
// driver instance with init/draw callbacks plus coords, a
// graphic surface (currently a borderless click-through
// transparent window), and per-mouse event handler vectors
// for clicks and moves. Drivers (e.g. PS/2 in L137) register
// via mouse_register.

#include "lib/vector/vector.h"

#define MOUSE_GRAPHIC_DEFAULT_WIDTH   10
#define MOUSE_GRAPHIC_DEFAULT_HEIGHT  10
#define MOUSE_GRAPHIC_ZINDEX          100000

enum {
    MOUSE_NO_CLICK,
    MOUSE_LEFT_BUTTON_CLICKED,
    MOUSE_RIGHT_BUTTON_CLICKED,
    MOUSE_MIDDLE_BUTTON_CLICKED
};

typedef int MOUSE_CLICK_TYPE;

struct mouse;
typedef int  (*MOUSE_INIT_FUNCTION)(struct mouse* mouse);
typedef void (*MOUSE_DRAW_FUNCTION)(struct mouse* mouse);

typedef void (*MOUSE_CLICK_EVENT_HANDLER_FUNCTION)(struct mouse* mouse,
                                                   int clicked_x, int clicked_y,
                                                   MOUSE_CLICK_TYPE type);
typedef void (*MOUSE_MOVE_EVENT_HANDLER_FUNCTION)(struct mouse* mouse,
                                                  int moved_to_x, int moved_to_y);

struct window;

struct mouse {
    MOUSE_INIT_FUNCTION  init;
    MOUSE_DRAW_FUNCTION  draw;
    char                 name[20];

    struct {
        int x;
        int y;
    } coords;

    // Mouse graphic surface. window_create owns it; the
    // CLICK_THROUGH + BORDERLESS + BACKGROUND_TRANSPARENT
    // flag combo keeps it out of the input path.
    struct {
        struct window* window;
        int            width;
        int            height;
    } graphic;

    struct {
        struct vector* click_handlers;  // MOUSE_CLICK_EVENT_HANDLER_FUNCTION
        struct vector* move_handlers;   // MOUSE_MOVE_EVENT_HANDLER_FUNCTION
    } event_handlers;

    void* private;
};

#endif
