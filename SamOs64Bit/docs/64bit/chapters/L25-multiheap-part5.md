# Lecture 25 - multi-heap part 5

**Source commit (PeachOS64BitCourse):** `f82b877`
**SamOs commit:** L25 on `module1-64bit` branch
**Regression test:** `tests64/L25-multiheap-routing-helpers.sh`

## Why this chapter exists

L20 - L24 built a multi-heap where every sub-heap is independent.
The next big problem is `kfree(ptr)`: given a bare pointer, which
sub-heap owns it? L25 builds the address-routing primitives that
answer the question. None of them are called yet - upstream
deliberately commits the helpers in a separate small lecture so
the wiring lecture (later in the arc) can focus on the actual
free / zalloc routing.

Three helpers land:

1. `heap_is_address_within_heap(heap, ptr)` - per-sub-heap range
   check using the `eaddr` field added in L20.
2. `multiheap_get_heap_for_address(mh, ptr)` - linear scan over
   the linked list, returns the owning sub-heap.
3. `multiheap_get_max_memory_end_address(mh)` - returns the
   highest eaddr across all sub-heaps. Will become the base of
   the "virtual arena" the defragmenter exposes.

Plus a `max_end_data_addr` field on `struct multiheap` and two
companion helpers (`multiheap_is_address_virtual`,
`multiheap_virtual_address_to_physical`) that interpret pointers
ABOVE `max_end_data_addr` as belonging to the virtual arena.

## Theory primer: physical vs virtual arena

The eventual defragment strategy:

- Each sub-heap covers a contiguous PHYSICAL range, with no
  guarantee of contiguity between sub-heaps.
- A defragmenter that's allowed to manipulate page tables can
  expose a CONTIGUOUS VIRTUAL range that aliases free pages
  scattered across multiple sub-heaps.
- That virtual range lives ABOVE the highest physical sub-heap
  (so the address space is unambiguous: anything below
  `max_end_data_addr` is a real physical pointer in some
  sub-heap; anything above is a virtual alias).

The L25 helpers let `kfree(ptr)` figure out which case it's in:

```c
void kfree(void* ptr) {
    if (multiheap_is_address_virtual(mh, ptr)) {
        // tear down the page tables for ptr's virtual arena range
        // and free the underlying physical blocks individually
    } else {
        struct multiheap_single_heap* owner =
            multiheap_get_heap_for_address(mh, ptr);
        heap_free(owner->heap, ptr);
    }
}
```

L25 does not implement the kfree wiring - that lands later. It
just makes sure the primitives exist so the wiring lecture is
small.

## The `<=` in heap_is_address_within_heap

```c
return ptr >= heap->saddr && ptr <= heap->eaddr;
```

Note the `<=`. `heap->eaddr` is the end boundary the caller
passed to `heap_create` - typically one past the last addressable
byte. So `ptr == eaddr` is technically a sentinel, not a valid
address. Why include it?

`multiheap_get_max_memory_end_address` returns the max `eaddr`,
and `multiheap_is_address_virtual` checks `ptr >=
max_end_data_addr`. We want the boundary itself to be the
crossover - if a sub-heap's `eaddr` is exactly the highest, then
any pointer == that address is virtual. The inclusive `<=`
matches that boundary convention.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/heap/heap.c` | new `heap_is_address_within_heap`. |
| `src/memory/heap/heap.h` | include `<stdbool.h>`; declare the new function. |
| `src/memory/heap/multiheap.h` | new `max_end_data_addr` field on `struct multiheap`. |
| `src/memory/heap/multiheap.c` | new static `multiheap_heap_allows_paging` (flag check), new `multiheap_get_max_memory_end_address`, `multiheap_get_heap_for_address`, `multiheap_is_address_virtual`, `multiheap_virtual_address_to_physical`. None called yet. |

## How we verified

VGA after L25:

```
Hello 64-bit!
e820 total: 267910144
ABCMemory wasted
```

Same tokens as L23 / L24 - the helpers are dormant. A regression
in the multiheap during the refactor would have either failed
to drain (no "Memory wasted") or faulted on paging_switch (no
"ABC"). Both made it through.

## Forward look

L26 - L30 wire the helpers: zalloc routing, free routing, the
defragment-by-paging second pass, and callback handlers for
heaps with custom ownership semantics. L31 implements
`paging_get`.
