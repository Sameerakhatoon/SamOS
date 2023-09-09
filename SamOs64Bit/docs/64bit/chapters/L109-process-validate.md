# Lecture 109 - process memory mgmt part 2 (validate)

L109 fills in the L107 stub
`process_validate_memory_or_terminate` and introduces the two
helpers it needs.

## `process_is_stack_memory(process, addr)`

True when `addr` lies inside
`[SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_END,
SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START]`. The user stack
grows down, so START is the high address and END is the low
one.

## `process_get_allocation_by_addr(process, addr, out)`

Range query. Fills `out` with the matching record plus a
`peek` block:

- If `addr` is in the stack region, the request describes the
  whole stack; `peek.total_bytes_left` is the gap between
  `addr` and the high end of the stack region. The
  `PROCESS_ALLOCATION_REQUEST_IS_STACK_MEMORY` flag is set.
- Otherwise walk the per-process `allocations` vector. For
  each entry, check `allocation.ptr <= addr <= allocation.end`
  inclusive. On match copy the allocation, set
  `peek.addr/.end/.total_bytes_left`, and return 0.

Returns `-EIO` when no allocation covers the address.

## `process_validate_memory_or_terminate(process, virt_addr, space_needed)`

Real body replaces the L107 one-liner stub:

```c
process_allocation_request request;
process_get_allocation_by_addr(process, virt_addr, &request);
if(request.peek.total_bytes_left < space_needed){
    process_terminate(process);
    return -EINVARG;
}
```

Failure (no covering allocation OR insufficient remaining
bytes) terminates the process. Used by the L107 `process_fread`
to make sure a user buffer + count fits inside ONE allocation.

## `process_terminate` teardown

Picks up `vector_free(process->allocations)` at the very end,
after `process_terminate_allocations` has drained the contents.
Mirrors upstream's `process_free_process`.

## Upstream bug preserved

`process_get_allocation_by_addr` computes the heap-allocation
`bytes_left` as

```c
size_t bytes_used = (uint64_t)addr - allocation_addr;
size_t bytes_left = allocation_addr_end - bytes_used;
```

The intent was `allocation_addr_end - (uint64_t)addr`. The
upstream form yields the correct value only when `addr ==
allocation.ptr` (bytes_used == 0). For any addr inside the
allocation, `bytes_left` is overstated by `2 * (addr -
allocation.ptr)`. The validate path therefore returns "ok" in
cases where the buffer + count actually overflows.

We preserve the bug verbatim and document it inline. The
L110 fread test calls fopen + fread on a freshly allocated
buffer so the addr always equals the buffer start; the bug
is inert there.

## BIOS test status

Source-only. Test verifies the new helpers exist, the validate
body walks via get_allocation_by_addr, the header is updated,
the stack constants are referenced, vector_free of allocations
happens at teardown, and the build links.
