# Ch 46 - Memory Allocation in Kernel Development

**Book pages:** 145-146 (Part 5)
**Code in this chapter:** none, prose

## Two kinds of memory the kernel uses

### Stack

Per-thread, fixed size, managed by the compiler. Allocation is implicit (every `int x;` lives on the stack), free is implicit (function return reclaims it). Cheap (just bump a register) but rigid:

- The size cap is set at thread creation. Overflow corrupts whatever lies below.
- You cannot return a stack pointer to a caller and expect it to stay valid.
- Each task needs its own stack region.

### Heap

A region of memory the kernel manages explicitly. Allocations come in arbitrary sizes, can outlive the function that allocated them, and need to be freed by name. Cost: bookkeeping. The kernel maintains data structures that track which bytes are in use and which are free.

## What the kernel has to do for itself

In user space the OS provides `malloc` / `free` (which themselves talk to `brk` or `mmap`). Inside the kernel there is no such service to fall back on, the kernel IS the service. We have to implement:

- A pool of memory to allocate from.
- A way to record which blocks of that pool are in use.
- An allocate function that finds a free run of N bytes, marks them used, returns a pointer.
- A free function that marks the corresponding block(s) free.
- A policy for what happens when the pool runs out.

## What we will build over the next few chapters

A simple block-based allocator:

- Pool: 100 MiB at physical 0x01000000 (16 MiB) onwards.
- Block size: 4096 bytes (one page). Allocations are rounded up to the next 4 KiB.
- Block table: a parallel byte array with one entry per block. Bits encode "taken/free", "first block of an allocation", "more blocks follow".
- `kmalloc(size)`: scan the table for `ceil(size / 4096)` consecutive free entries, mark them, return the pool address that corresponds to the start.
- `kfree(ptr)`: from the pool address, compute the table index, walk the "has next" chain marking blocks free.

Page-sized blocks mean that once we turn paging on, every allocation lines up neatly with a page table entry. No splitting pages between two allocations.

## What we will NOT build

- No best-fit, no buddy allocator, no slab cache. First-fit linear scan.
- No fragmentation defense beyond "blocks are all the same size".
- No SMP (single CPU only, so no locks).
- No statistics or debugging metadata (those can be layered on later).

This is the simplest design that gets the kernel a working heap. Sophistication can come if we ever need it.
