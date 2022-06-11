# Ch - Memory Management & Virtual Memory

**Book page:** 8 (Part 2 - The Basics)
**Code in this chapter:** none - prose

## What the kernel actually does for memory

- Tracks which bytes are in use and by whom.
- Allocates on request (`malloc`-shaped APIs in user space; `kmalloc`-shaped in kernel space).
- Reclaims on free; coalesces neighbouring free blocks to fight fragmentation.
- Enforces protection - one process can't read or write another's memory unless explicitly shared.

## Virtual memory in one paragraph

Each process sees its own contiguous address space starting at virtual address 0 (in practice some low region is reserved). The CPU's MMU walks page tables to translate every virtual address the process touches into a physical address. Two wins:

1. **Programs stay simple** - they don't worry about where they live physically; the kernel can move them, swap them, or page them out without the program noticing.
2. **Isolation** - process A's virtual address `0x4000_0000` and process B's virtual address `0x4000_0000` map to different physical pages. They can't accidentally read each other.

## Where this lands in SamOs

We won't implement paging for many chapters yet. But everything we do - boot loader, GDT, IDT, kernel heap - sets up the runway. The first time we flip CR0.PG (turn paging on), every line of code we write before that will run with the MMU disabled.
