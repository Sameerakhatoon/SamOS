# Lecture 107 - userland fread

L107 hangs `fread` off the L105/L106 file syscall chain.

## Kernel-side

- `SYSTEM_COMMAND12_FREAD`.
- `isr80h_command12_fread` reads four stack items: buffer
  (item 0), size (item 1), count (item 2), fd (item 3). The
  buffer is left task-virtual; translation happens inside
  `process_fread`.
- `process_fread` looks the fd up on the process's
  `file_handles` vector, calls
  `process_validate_memory_or_terminate` for the read region,
  translates the buffer through
  `task_virtual_address_to_physical`, and forwards to the
  kernel `fread`.

## `process_validate_memory_or_terminate` stub

Upstream's L107 calls `process_validate_memory_or_terminate`,
but the function is not introduced until L109 (along with the
allocation-tracking infrastructure it depends on,
`process_get_allocation_by_addr` and
`process_allocation_request`). The function would otherwise
fail to link.

We add a stub now that returns `0` (validation passes); L109
will replace the body with the upstream walk:

```c
struct process_allocation_request allocation_request;
process_get_allocation_by_addr(process, virt_addr, &allocation_request);
if(allocation_request.peek.total_bytes_left < space_needed){
    return -EINVARG;
}
// terminate the process on failure
```

The stub is annotated inline with an explicit "Lecture 107-stub"
header and a pointer to L109.

## Userland-side

- `samos.asm` gains `samos_read`. Push order: fd, count, size,
  buffer (top of stack is buffer = item 0).
- `samos.h` and `stdlib/file.h` declare the C wrappers.
- `stdlib/file.c::fread(buffer, size, count, fd)` is a one-line
  forwarder.

The upstream L106 peachos_fclose bug (missing `int 0x80`)
remains; L107 does not touch it.

## What boots will do (with sysfont.bmp present)

Same as L106 - blank.c calls fopen and reports success/fail.
L110 will replace blank.c with an actual fread test.

## BIOS test status

Source-only. Verifies every new symbol exists, the stub is in
place, and the build links.
