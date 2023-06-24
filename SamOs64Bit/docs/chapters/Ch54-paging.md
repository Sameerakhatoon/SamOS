# Ch 50 - Understanding paging in 32-bit protected mode

**Book pages:** 169-177 (Part 5)
**Code in this chapter:** none, prose

## Why paging exists

Real Mode and bare Protected Mode let every program see the same physical address space. Programs A and B both think address 0x4000 is their own; if A writes there it stomps on B. The fix is to give each task its own virtual address space and have the CPU translate virtual addresses to physical ones on every memory access.

The MMU does that translation. Paging tells the MMU how.

## Page size

Two options on x86: 4 KiB pages or 4 MiB pages. We use 4 KiB pages, which matches our heap block size. Each page is a 4 KiB chunk of memory described by a Page Table Entry (PTE).

## The two-level structure

```
CR3 -> Page Directory  (1024 entries, each 4 bytes -> 4 KiB total)
        |
        +-> Page Directory Entry [i]  =>  Page Table   (1024 entries, 4 bytes each)
                                          |
                                          +-> Page Table Entry [j]  =>  4 KiB page
```

- The Page Directory is itself a 4 KiB block.
- Each Page Directory Entry (PDE) points to a Page Table, which is also 4 KiB.
- Each Page Table Entry (PTE) points to a 4 KiB physical page.
- Total reachable: 1024 PDEs * 1024 PTEs * 4 KiB = 4 GiB. That's exactly the 32-bit address space.

The CPU finds the page tables by reading the physical address out of the CR3 register. Every task switch loads a different CR3, which means every task gets its own page directory and therefore its own virtual layout.

## How a virtual address gets translated

A 32-bit virtual address is split:

```
| 10 bits | 10 bits | 12 bits |
| PD idx  | PT idx  | offset  |
```

The MMU:

1. Reads CR3 to find the Page Directory's physical address.
2. Indexes the directory with the top 10 bits to get a Page Directory Entry.
3. PDE has the physical address of a Page Table in its top 20 bits. Index the table with the next 10 bits to get a Page Table Entry.
4. PTE has the physical address of a 4 KiB page in its top 20 bits. Append the bottom 12 bits of the virtual address as offset. That is the final physical address.

Bottom 12 bits of each entry's high half are zero by construction because pages are 4 KiB aligned.

## PTE flag bits

The low 12 bits of a PTE hold flags:

| Bit | Name | Meaning |
|-----|------|---------|
| 0   | P (Present)        | 1 = page is in memory. 0 = access triggers #PF. |
| 1   | R/W (Read/Write)   | 1 = writable. 0 = read-only (unless CR0.WP allows kernel to bypass). |
| 2   | U/S (User/Supervisor) | 1 = ring 3 may access. 0 = ring 0 only. |
| 3   | PWT (Write-through)| 1 = cache write-through; 0 = write-back. |
| 4   | PCD (Cache disable)| 1 = no caching for this page. |
| 5   | A (Accessed)       | CPU sets to 1 on first access; OS clears if it wants. |
| 6   | D (Dirty)          | CPU sets to 1 on first write. |
| 7   | PAT                | with PCD/PWT, picks the cache type (else reserved 0). |
| 8   | G (Global)         | 1 = TLB entry survives CR3 reload (requires CR4.PGE=1). |
| 9-11 | AVL                | available for OS use. |
| 12-31 | base address      | upper 20 bits of the page's physical address. |

PDE flags are mostly the same, plus a `PS` (Page Size) bit at position 7 which we leave 0 (we use 4 KiB, not 4 MiB).

## Why paging interacts with our heap

The kernel heap blocks are 4 KiB each. The pages the MMU manages are also 4 KiB. So a `kmalloc(4096)` returns memory that aligns exactly to one page. Permissions and presence can be set per-allocation without splitting.

Once paging is on, the heap pool at virtual `0x01000000` (16 MiB) needs page table entries that map those virtual addresses to actual physical RAM. If we identity-map the bottom 4 GiB to itself, kmalloc keeps working unchanged because virtual == physical.

That is the strategy the book takes for now: build a "linear" or "identity" mapping where virtual address X maps to physical address X for the whole 4 GiB. Tasks come later with their own per-process directories.

## Page faults

If the MMU dereferences a PTE with P=0, or violates the W or U/S bits, the CPU raises a Page Fault exception (#PF, vector 14). The kernel's #PF handler can:

- Load the missing page from disk (demand paging / swap).
- Allocate a fresh zeroed page (copy-on-write).
- Kill the offending task (segfault).

For now we will not have a #PF handler beyond the IDT's `no_interrupt` stub. The plan is to set up the identity map so we never get faults during normal operation.

## What the next chapter does

- Adds `src/memory/paging/` with `paging.h`, `paging.c`, `paging.asm`.
- C functions to allocate a 4 KiB page directory, fill it with 1024 page tables, fill those with 1024 PTEs each (4 MB of identity-mapped memory per PDE).
- Asm wrappers around `mov cr3, eax` (paging_load_directory) and `mov cr0, eax | 0x80000000` (enable_paging).
- kernel_main builds the identity-mapping directory and enables paging.

After that we are running under the MMU but with virtual == physical, which is functionally invisible until per-process tables arrive.
