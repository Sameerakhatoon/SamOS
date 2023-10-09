# Lecture 135 - graphics_info_recalculate

L135 implements `graphics_info_recalculate(g)` in graphics.c.
Called by L134's `window_position_set` after the root's
relative coords change so every descendant surface lands at
the right absolute coordinate.

## Body

```c
void graphics_info_recalculate(struct graphics_info* g){
    if(g->parent){
        g->starting_x = g->relative_x + g->parent->starting_x;
        g->starting_y = g->relative_y + g->parent->starting_y;
    }
    if(g->children){
        for each child: graphics_info_recalculate(child);
    }
}
```

A surface without a parent (the screen root) skips the
position update but still recurses into its children. The
recursion is bottom-up via the children vector built by L116's
`graphics_info_create_relative`.

## L134 stub retired

`window.c`'s L134 `__attribute__((weak)) graphics_info_recalculate`
fallback is removed. The L136 `window_redraw` weak stub stays
until that lecture lands.

## BIOS test status

Source + link. Test asserts the body is in graphics.c, the
prototype is in graphics.h, the body actually recurses, and
the weak stub is gone from window.c. The build links.
