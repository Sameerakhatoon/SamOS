#ifndef GRAPHICS_WINDOW_H
#define GRAPHICS_WINDOW_H
// Lecture 118 - Window header.
//
// The window subsystem is a layer over the L116 graphics
// helpers: each window owns a `root_graphics` surface (a
// graphics_info_create_relative under the desktop), a
// title_bar_graphics child, and a content graphics surface;
// each carries its own terminal. Event delivery (focus,
// mouse, key) flows through a per-window vector of
// WINDOW_EVENT_HANDLER callbacks.
//
// L118 lands the shapes only. The implementation (window.c)
// is module-3 work; for now the header documents the contract
// that L116's primitives will need to satisfy.

#include "graphics/terminal.h"
#include "graphics/graphics.h"
#include "config.h"
#include <stddef.h>
#include <stdint.h>

struct window;

enum {
    WINDOW_EVENT_TYPE_NULL,
    WINDOW_EVENT_TYPE_FOCUS,
    WINDOW_EVENT_TYPE_LOST_FOCUS,
    WINDOW_EVENT_TYPE_MOUSE_MOVE,
    WINDOW_EVENT_TYPE_MOUSE_CLICK,
    WINDOW_EVENT_TYPE_WINDOW_CLOSE,
    WINDOW_EVENT_TYPE_KEY_PRESS
};

struct window_event {
    int             type;
    int             win_id;
    struct window*  window;
    union {
        struct {
            // No extra data for focus / lost-focus.
        } focus;
        struct {
            int x;
            int y;
        } move;
        struct {
            int x;
            int y;
        } click;
        struct {
            int key;
        } keypress;
    } data;
};

// Lecture 160 - userland-facing copy of struct window_event.
// The userland struct mirrors the data union but omits the
// kernel-only keypress slot; window_event_to_userland enforces
// the data-region sizes match.
struct window_event_userland {
    int             type;
    int             win_id;
    struct window*  window;
    union {
        struct {
            // No extra data for focus.
        } focus;
        // positions are relative to the window body.
        struct {
            int x;
            int y;
        } move;
        // relative to the window body.
        struct {
            int x;
            int y;
        } click;
    } data;
};

typedef int (*WINDOW_EVENT_HANDLER)(struct window* window, struct window_event* event);

enum {
    WINDOW_FLAG_BORDERLESS              = 0b00000001,
    WINDOW_FLAG_CLICK_THROUGH           = 0b00000010,
    WINDOW_FLAG_BACKGROUND_TRANSPARENT  = 0b00000100
};

struct window {
    int               id;
    struct terminal*  title_bar_terminal;
    // Body terminal (under title_bar_terminal).
    struct terminal*  terminal;

    // Surfaces. root_graphics covers the whole window (border +
    // title bar + body); title_bar_graphics is the strip across
    // the top; graphics is the body region.
    struct graphics_info*  root_graphics;
    struct graphics_info*  title_bar_graphics;
    struct graphics_info*  graphics;

    struct {
        struct {
            size_t x;
            size_t y;
            size_t width;
            size_t height;
        } close_btn;
    } title_bar_components;

    struct {
        // vector of WINDOW_EVENT_HANDLER
        struct vector* handlers;
    } event_handlers;

    size_t width;
    size_t height;
    size_t x;
    size_t y;

    // Compositor draw order; matches graphics_info.z_index for
    // root_graphics.
    size_t zindex;

    char   title[WINDOW_MAX_TITLE];
    int    flags;
};

// Lecture 122 - public window API.
int             window_system_initialize(void);
int             window_system_initialize_stage2(void);
void            window_set_z_index(struct window* window, int zindex);
void            window_unfocus(struct window* old_focused_window);
void            window_focus(struct window* window);
struct window*  window_create(struct graphics_info* graphics_info,
                              struct font* font,
                              const char* title,
                              size_t x, size_t y,
                              size_t width, size_t height,
                              int flags, int id);

// Lecture 123 - event handler register / unregister.
void            window_event_handler_register(struct window* window,
                                              WINDOW_EVENT_HANDLER handler);
void            window_event_handler_unregister(struct window* window,
                                                WINDOW_EVENT_HANDLER handler);
// Lecture 136 - graphics_redraw the window's root surface.
void            window_redraw(struct window* window);
// Lecture 138 - body-terminal accessor used by the mouse
// module's default draw.
struct terminal* window_terminal(struct window* window);

// Lecture 134 - move a window.
int             window_position_set(struct window* window, size_t new_x, size_t new_y);

// Lecture 144 - push a MOUSE_CLICK event into the window's
// handler chain. window-relative coords.
void            window_click(struct window* window, int rel_x, int rel_y,
                             MOUSE_CLICK_TYPE type);

// Lecture 145 - helpers.
bool            window_owns_graphics(struct window* win,
                                     struct graphics_info* graphics);
struct window*  window_focused(void);
struct window*  window_get_from_graphics(struct graphics_info* graphics);
void            window_redraw_region(struct window* window,
                                     int x, int y, int width, int height);
void            window_redraw_body_region(struct window* window,
                                          int x, int y, int width, int height);
void            window_title_set(struct window* window, const char* title);

// Lecture 149 - dispatch an event to every handler registered
// on the window. Used by L143-L148 dispatch sites and by
// userspace code in L153+.
void            window_event_push(struct window* window, struct window_event* event);

// Lecture 154 - close a window. Frees the surface, drops the
// node from the compositor list, releases the title bar.
void            window_close(struct window* window);

// Lecture 160 - copy a kernel window event into its userland
// projection. Panics if the data-union sizes diverge.
void            window_event_to_userland(struct window_event* kernel_win_event_in,
                                         struct window_event_userland* userland_win_event_out);

#endif
