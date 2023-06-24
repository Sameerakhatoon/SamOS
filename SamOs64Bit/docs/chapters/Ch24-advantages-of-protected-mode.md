# Ch 20 - Advantages of Protected Mode

**Book page:** 47 (Part 5)
**Code in this chapter:** none, prose

## Six features the book lists

1. **Extended memory access.** 4 GB instead of 1 MB on 32-bit; way more on 64-bit. Address bus is 32 bits wide so segment:offset arithmetic does not have to fold into 20 bits.
2. **Memory protection.** Segments have access bits (read-only, read-write, execute). Page tables add per-page permissions. The CPU raises a page fault or general protection fault on violation instead of silently corrupting memory.
3. **Virtual memory and paging.** Each process can have its own 4 GB virtual address space. The MMU translates virtual addresses to physical via page tables. Disk-backed swap is possible.
4. **Hardware-based multitasking.** The CPU has support for TSS-based task switches (load CR3, swap saved registers, jump to new EIP) all in a single instruction. Most modern OSes do not use the CPU's hardware task switch and roll their own software scheduler, but the support is there.
5. **I/O protection.** Each task can have an IOPB (I/O permission bitmap) in its TSS that controls which I/O ports the task can `in`/`out` from. Ring 0 (kernel) typically has all access; ring 3 (user) typically has none.
6. **32-bit instructions and wider registers.** EAX is 32 bits where AX was 16. New instructions (`movzx`, `movsx`, `bsf`, `bsr`, `cdq`, `cwde`, etc.). Access to FPU, MMX, SSE, AVX as they were added in later CPU generations.

## Where these will land in SamOs

- Memory protection: when we set up the GDT with ring 3 data and code segments and have user programs.
- Virtual memory: when we build the page tables and turn on CR0.PG.
- Multitasking: when we implement the timer-driven scheduler that swaps tasks.
- I/O protection: implicit, since we never give user programs `inb`/`outb` access.
- 32-bit instructions: enabled by the far jump into the 32-bit code segment, in the very next chapter.
