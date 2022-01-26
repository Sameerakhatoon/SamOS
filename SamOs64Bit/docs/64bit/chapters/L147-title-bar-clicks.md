# Lecture 147 - title bar clicks

L147 wires the title bar surface to a click router and adds
the close-or-drag dispatch.

## `window_title_bar_clicked`

```c
struct window* win = window_get_from_graphics(title_graphics);
if(win){
    close_btn rect = win->title_bar_components.close_btn;
    if((rel_x, rel_y) is inside close_btn){
        window_close(win);
    }else if(window_moving == win){
        window_moving = NULL;   // toggle off
    }else{
        window_moving = win;    // start dragging
    }
}
```

The `window_moving` toggle is what L148 will pick up on
move events.

## Wiring

`window_create` adds one line after the title bar surface is
built:

```c
graphics_click_handler_set(title_bar_graphics_info, window_title_bar_clicked);
```

So clicks on the title bar route through this function via
the L141 click bubble.

## BIOS test status

Source + link. Test confirms the router body, both branches
(close + drag), and the wiring inside `window_create`. Build
links.
