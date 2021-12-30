# Lecture 114 - process slot table moves to a vector

L114 retires the fixed-size `static struct process*
processes[SAMOS_MAX_PROCESSES]` table on `process.c` and
replaces it with a `struct vector* process_vector`. The
process count is now bounded only by available memory.

## Pieces

### `process_vector`

Module global initialised by the new `process_system_init`:

```c
process_vector = vector_new(sizeof(struct process*), 10, 0);
```

Capacity of 10 matches upstream.

### `process_get(id)`

Forwards through `vector_at`. Out-of-range id returns
`ERROR(EINVARG)` rather than NULL so the callers can keep
ISERR(). The slot value itself can still be NULL (free).

### `process_get_free_slot()`

Walks the vector for a NULL slot. If none found, push a fresh
NULL and return the new index. This is what makes the table
grow on demand.

### `process_load_for_slot(...)`

`process_get_free_slot` already reserved the index; this
function `vector_overwrite`s the slot with the new process
pointer instead of writing `processes[i] = _process`.

### `process_switch_to_any` and `process_unlink`

Both are vector-walks now. `process_unlink` writes a NULL
through `vector_overwrite` and only triggers a
`process_switch_to_any` if the unlinked process was current.

## `kernel.c`

`kernel_main` calls `process_system_init()` between
`tss_load` and `isr80h_register_commands`. The ordering
matters: any later code path that takes a process slot would
deref a NULL `process_vector`.

## SAMOS_MAX_PROCESSES

The constant stays defined in `config.h` for compatibility,
but `process.c` no longer references it. Any external module
that still loops `[0 .. SAMOS_MAX_PROCESSES)` would get back
NULLs for empty slots and would only see however many slots
the vector has been pushed to; if there are no consumers like
that today, the constant is effectively dead.

## BIOS test status

Source-only. Test verifies the static array is gone, no array
indexing on `processes[]` remains in code, the vector + init
exist, the kernel init order is right, and the build links.
