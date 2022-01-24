# Lecture 144 - window_click

L144 lands the real `window_click` body. Drops the L143 weak
stub.

## Body

```c
void window_click(struct window* window, int rel_x, int rel_y,
                  MOUSE_CLICK_TYPE type){
    struct window_event event = {0};
    event.type         = WINDOW_EVENT_TYPE_MOUSE_CLICK;
    event.data.click.x = rel_x;
    event.data.click.y = rel_y;
    window_event_push(window, &event);
}
```

Stamps a `WINDOW_EVENT_TYPE_MOUSE_CLICK` event and pushes it
through the L123 event-handler chain. The default L123
handler ignores it; L146+ wires real listeners.

## Forward decl

`window_event_push` lives further down the file. We
forward-declare it just above `window_click` so the order
stays cosmetic-matching upstream's hunk placement.

## window.h

Adds the prototype next to the L134 `window_position_set`
decl.

## BIOS test status

Source + link. Test asserts the real body, the
`WINDOW_EVENT_TYPE_MOUSE_CLICK` reference, the header decl,
that the weak stub is gone, and the build links.
