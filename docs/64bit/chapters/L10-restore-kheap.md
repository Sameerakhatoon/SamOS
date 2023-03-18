# Lecture 10 - restore heap functionality

**Source commit (PeachOS64BitCourse):** `cf74370`
**SamOs commit:** L10 on `module1-64bit` branch
**Regression test:** `tests64/L10-kheap-online.sh`
**Port fix:** `(unsigned int)ptr` → `(uintptr_t)ptr` in `heap.c::heap_validate_alignment`

## Why this chapter exists

The kernel can print, but everything it knows is statically
allocated. Lecture 10 brings back the heap so `kmalloc` /
`kfree` work and `kernel_main` can ask for memory at runtime.

The heap code itself is the same data structure as the 32-bit
SamOs heap (a block-table allocator, one bit per 4 KiB block,
table at 0x7E00, body at 0x01000000, 100 MiB total). The change
of significance for this branch is one cast - see the **port
fix** below.

## Theory primer

### 1. SamOs heap, in one paragraph

The kheap is one contiguous region of physical memory split into
fixed-size **blocks** (4 KiB each). A side **table** has one byte
per block: 0x01 = "block is free", 0x41 = "block is taken AND is
the first of a contiguous run", 0x81 = "block is taken AND is the
last of a run". `kmalloc(n)` searches for `ceil(n / 4096)` free
blocks in a row, marks them taken (HAS_NEXT + IS_FIRST + IS_LAST
patterns), returns the address. `kfree(ptr)` walks the table
forward from `ptr` until it hits an IS_LAST cell, marking each
back to free.

Address constants (`SAMOS_HEAP_*` in `src/config.h`):

| Symbol | Value | Meaning |
|---|---|---|
| `SAMOS_HEAP_TABLE_ADDRESS` | `0x00007E00` | byte table lives just after the BIOS data area |
| `SAMOS_HEAP_ADDRESS`       | `0x01000000` | heap body starts at 16 MiB |
| `SAMOS_HEAP_BLOCK_SIZE`    | `4096` | block size in bytes |
| `SAMOS_HEAP_SIZE_BYTES`    | `104857600` | 100 MiB heap |

Both addresses fall inside the PD_Table identity map we extended
to 130 MiB this lecture (see "PD growth" below). The CPU sees
these as ordinary 64-bit virtual addresses that are identity-
mapped to physical, so no paging code is needed yet.

### 2. The port fix - `unsigned int` vs `uintptr_t`

`heap_validate_alignment` in the 32-bit SamOs read:

```c
return ((unsigned int)ptr % SAMOS_HEAP_BLOCK_SIZE) == 0;
```

On x86_64, `void*` is 8 bytes but `unsigned int` is 4 bytes. The
cast truncates: a pointer like `0xFFFFFFFF80100000` (a kernel
address above 4 GiB) becomes `0x80100000` and the alignment check
runs on the wrong value.

`-Werror=pointer-to-int-cast` catches this at compile time, which
is how I noticed during the first build attempt:

```
heap.c:31: error: cast from pointer to integer of different size
   31 |     return ((unsigned int)ptr % SAMOS_HEAP_BLOCK_SIZE) == 0;
```

The fix is `uintptr_t` - a `<stdint.h>` type guaranteed to be
exactly the platform pointer width:

```c
return ((uintptr_t)ptr % SAMOS_HEAP_BLOCK_SIZE) == 0;
```

This is the same change PeachOS64's L10 commit makes to its
copy of heap.c. We get to keep the rest of the file unchanged.

### 3. PD growth - 65 entries via NASM `%rep`

L8's PD_Table had three 2-MiB leaves (12 MiB total). The heap
sits at 0x01000000 - past those three leaves. Without a bigger
identity map, the first `kmalloc` write would `#PF`.

We use NASM's `%rep` macro to emit 65 entries in a loop. Each
covers 2 MiB, so the total is 130 MiB, well above the heap
body's 0x01000000 + 100 MiB ceiling:

```nasm
%define PS_FLAG 0x83                ; P | RW | PS=1
%define PAGE_INCREMENT 0x200000     ; 2 MiB

align 4096
PD_Table:
    %assign addr 0x00000000
    %rep 65
        dq addr + PS_FLAG
        %assign addr addr + PAGE_INCREMENT
    %endrep
    times 447 dq 0
```

After L10 the identity map covers `0..0x82FFFFF` (130 MiB).

### 4. What `kheap_init` actually does

```c
void kheap_init(void)
{
    int total_table_entries = SAMOS_HEAP_SIZE_BYTES / SAMOS_HEAP_BLOCK_SIZE;
    kernel_heap_table.entries = (HEAP_BLOCK_TABLE_ENTRY*) SAMOS_HEAP_TABLE_ADDRESS;
    kernel_heap_table.total   = total_table_entries;

    void* end = (void*)(SAMOS_HEAP_ADDRESS + SAMOS_HEAP_SIZE_BYTES);
    int res = heap_create(&kernel_heap, (void*) SAMOS_HEAP_ADDRESS, end,
                          &kernel_heap_table);
    if (res < 0) { /* nothing yet - panic isn't back online */ }
}
```

Two memsets later (one on the heap struct, one on the table to
mark every byte free), we have a live allocator and `kmalloc`
returns predictable addresses:

| Call | Returns |
|---|---|
| `kmalloc(100)` 1st | `0x01000000` |
| `kmalloc(8000)` 1st | `0x01001000` |
| `kfree(0x01000000) + kmalloc(100)` | `0x01000000` (reused) |

These exact addresses were the basis for `tests/13-kmalloc-works.sh`
on the 32-bit branch. They'll match here for the same reason.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/kernel.asm` | PD_Table moves from 3 hand-rolled entries to a `%rep 65` macro that covers 130 MiB |
| `src/kernel.c` | `#include "memory/heap/kheap.h"` and `"memory/memory.h"`. `kernel_main` calls `kheap_init()`, then `char* data = kmalloc(50)` writes "ABC\0" and `print`s it |
| `src/memory/heap/heap.c` | `(unsigned int)ptr` → `(uintptr_t)ptr` in `heap_validate_alignment`. One-line port fix |
| `Makefile` | `FILES +=` `./build/memory/memory.o`, `./build/memory/heap/heap.o`, `./build/memory/heap/kheap.o`; new build rules for each |
| `build.sh` | `mkdir -p build/memory build/memory/heap` |

`src/memory/memory.c` (`memset`, `memcpy`, `memcmp`) compiles
unchanged. `src/memory/heap/kheap.c` compiles unchanged after
the heap.c fix.

## How we verified

`tests64/L10-kheap-online.sh` boots, snapshots VGA, asserts both
`Hello 64-bit!` AND `ABC` are in the buffer. If `kheap_init`
faulted, the `print(data)` would never run; if `kmalloc` returned
NULL the deref of `data[0]` would `#PF` and the kernel would
reset (we run with `-no-reboot` so QEMU exits, no banner visible).

Observed VGA dump:

```
Hello 64-bit!
ABC
```

## Things to watch later

- We didn't port `kfree` to a regression test yet. The 32-bit
  branch had `km3=0x01411000` proving free-then-realloc reuses
  the slot. The same property holds here; we'll add it back when
  more of the heap API is exercised by user-mode tests.
- We're still single-threaded with interrupts off (we haven't
  rebuilt the IDT yet). When IRQs come back the heap path will
  need re-auditing for concurrent kmalloc + IRQ-time
  kmalloc cases (SamOs doesn't do allocator locking; the policy
  is "no kmalloc from interrupt context").
- `paging.c` has the same `(unsigned int)virt`/`(unsigned int)phys`
  truncation as line 31 of heap.c had. That file isn't in the
  build yet, so the warning hasn't fired. When Lecture 11 brings
  paging back, expect to land the same `uintptr_t` fix.

## Next lecture

Lecture 11 - switch the kernel-side paging from 2 MiB pages to
4 KiB pages. The 32-bit SamOs already used 4 KiB pages everywhere;
the long-mode bringup deliberately used 2 MiB leaves (PS=1) for
the boot path because it's the simplest mapping. From L11 we
move closer to "general-purpose paging API in C".
