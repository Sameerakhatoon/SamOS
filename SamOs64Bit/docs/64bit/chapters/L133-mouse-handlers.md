# Lecture 133 - mouse register/unregister handlers + draw

L133 adds the click + move handler register / unregister
helpers, a `mouse_draw` dispatcher, and the public API decls.

## Helpers

Four functions follow the same shape:

```c
void mouse_register_move_handler(struct mouse* mouse,
                                 MOUSE_MOVE_EVENT_HANDLER_FUNCTION h){
    if(mouse){
        vector_push(mouse->event_handlers.move_handlers, &h);
        return;
    }
    ...recurse across mouse_driver_vector...
}
```

- `mouse != NULL`: push/pop on the per-mouse vector.
- `mouse == NULL`: walk every registered mouse and recurse so
  one call wires (or removes) the handler on every driver.
- The panic on empty driver vector is per upstream;
  identifier typos ("driers", "reigstered") preserved
  verbatim.

## `mouse_draw`

```c
void mouse_draw(struct mouse* mouse){ mouse->draw(mouse); }
```

Explicit dispatch; the caller is responsible for having wired
`mouse->draw`. The L132 `mouse_draw_default_impl` is still the
fallback used inside `mouse_register`.

## Public API

`mouse.h` now exposes:

- `mouse_draw`, `mouse_register_*`, `mouse_unregister_*`.
- `mouse_moved`, `mouse_click`, `mouse_position_set`.
- `mouse_register`, `mouse_system_init`.

`mouse_draw_default_impl` and the static-driver loader stay
implementation-private.

## Upstream identifier typos preserved

- `"NO Mice driers are registered\n"` (sic, "driers")
- `"NO mice drivers are reigstered\n"` (sic, "reigstered")

Both carried with a `// sic - upstream typo` marker.

## Build state

`mouse.o` still NOT in FILES. mouse.c references
`window_position_set` (L134) and `window_terminal` (L145)
that are not yet defined.

## BIOS test status

Source-only. Test asserts every new API symbol exists in both
files, the recursion through `mouse_driver_vector` is wired,
the pop_element paths are present, and mouse.o is still out of
FILES.
