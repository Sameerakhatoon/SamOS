# Lecture 108 - process allocations move to a vector

L108 retires the fixed-size `process_allocation
allocations[SAMOS_MAX_PROGRAM_ALLOCATIONS]` array on
`struct process` and replaces it with a `struct vector*`. New
allocations grow the vector; freed slots are zeroed in place
and reused before a fresh `vector_push` extends the table.

This also lands the shape that L109 will use for the validation
walk (`struct process_allocation_request`) and the `end` field
on the per-allocation record.

## `struct process_allocation`

Gains `void* end` (set to `ptr + size` at allocation time).
Range queries can compare against `.end` directly without
adding `.size` to `.ptr` at every step.

## `struct process_allocation_request`

New. The shape

```c
enum {
    PROCESS_ALLOCATION_REQUEST_IS_STACK_MEMORY = 0b00000001,
};

struct process_allocation_request {
    struct process_allocation  allocation;
    int                        flags;
    struct {
        void*  addr;
        void*  end;
        size_t total_bytes_left;
    } peek;
};
```

is the descriptor L109's `process_get_allocation_by_addr`
returns. The `peek` block carries "how many bytes are left in
the matched allocation past the queried address", which is what
`process_validate_memory_or_terminate` (currently a L107 stub)
will use.

## `struct process` field

```c
struct vector* allocations;
```

replaces the array. `process_init` calls
`vector_new(sizeof(struct process_allocation), 10, 0)`. The
initial capacity of 10 matches upstream.

## New helpers

- `process_find_free_allocation_index`: walks the vector for a
  zero `.ptr` slot; if none found, pushes a zeroed slot and
  returns its index. Returns negative on push failure.
- `process_allocation_set_map`: paging_map_to + vector_at +
  vector_overwrite. Both `process_malloc` and any future
  reallocation path route through this.
- `process_get_allocation_by_start_addr`: scan for `ptr == addr`,
  copy the record into `*allocation_out`, return 0; -EIO when
  no match. The "by_start_addr" name is upstream verbatim. L109
  will add the related `process_get_allocation_by_addr` for
  range queries.

## `process_malloc`

Drops the inline paging_map_to + array write; calls
`process_allocation_set_map` instead.

## `process_free`

Switches from
`process_get_allocation_by_addr` (returns a `*` into the array)
to `process_get_allocation_by_start_addr` (fills a stack copy).
The struct copy is then used to compute the unmap range.

## `process_terminate_allocations`

Vector walk, skips zero-ptr slots.

## SamOs deviation: process_allocation_unjoin

Upstream's L108 commit leaves `process_allocation_unjoin` using
`process->allocations[i].ptr = 0` array syntax even though
`allocations` is now a `struct vector*`. The upstream binary
still compiles because indexing a `struct vector*` lands on the
adjacent `struct vector` records and `struct vector` has a `ptr`
field of its own. Behavior is silently wrong - the freed slot
never actually gets cleared.

We deviate: `process_allocation_unjoin` is rewritten to
`vector_at` + `vector_overwrite` so a freed slot really does
go back into the reuse pool. The deviation is documented
inline with an "upstream bug" comment block. Without this fix
L114's process list test would silently fail.

## BIOS test status

Source-only. Test asserts the new struct field, the new
request shape, the vector field on `struct process`, the new
helpers, no remaining array indexing on allocations, and that
the build links.
