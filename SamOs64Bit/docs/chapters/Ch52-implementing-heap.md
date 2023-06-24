# Ch 48 - Implementing our Heap

**Book pages:** 150-165 (Part 5)
**Code added:** `src/status.h`, `src/memory/heap/{heap.h,heap.c,kheap.h,kheap.c}`, config additions, `kheap_init()` wired into `kernel_main`, kmalloc smoke probe with hex print
**Test:** `tests/13-kmalloc-works.sh`

## What we built

A working kernel heap with `kmalloc(size)` and `kfree(ptr)`.

### Files

- **`src/status.h`** - error code constants (`SAMOS_ALL_OK = 0`, `EIO`, `EINVARG`, `ENOMEM`).
- **`src/memory/heap/heap.h`** - the generic heap interface: `struct heap`, `struct heap_table`, the three bit flags (TAKEN, IS_FIRST, HAS_NEXT), and three functions (`heap_create`, `heap_malloc`, `heap_free`).
- **`src/memory/heap/kheap.h`** - the kernel-side wrapper interface: `kheap_init`, `kmalloc`, `kfree`.
- **`src/memory/heap/kheap.c`** - creates one `struct heap` (the kernel heap) and one `struct heap_table` (the kernel heap's bookkeeping), wires their addresses from the config constants.

### Config additions

```c
#define SAMOS_HEAP_SIZE_BYTES   104857600   // 100 MiB
#define SAMOS_HEAP_BLOCK_SIZE   4096        // 4 KiB blocks
#define SAMOS_HEAP_ADDRESS      0x01000000  // pool starts at 16 MiB
#define SAMOS_HEAP_TABLE_ADDRESS 0x00007E00 // bookkeeping just past boot sector
```

Heap table size = `100 MiB / 4 KiB = 25600 bytes` of bookkeeping at 0x7E00. Heap pool at 0x01000000 (16 MiB), 100 MiB long.

### Kernel main now calls kheap_init

```c
void kernel_main(){
    terminal_initialize();
    print("Hello world!\ntest");

    kheap_init();
    idt_init();

    // kmalloc smoke probe:
    void* p1 = kmalloc(100);
    void* p2 = kmalloc(8000);
    // print km1=... km2=...
    kfree(p1);
    void* p3 = kmalloc(100);
    // print km3=... (should equal p1)
}
```

Plus a small `print_hex32` helper that emits a 32-bit value as 8 hex digits so the smoke probe results are visible on VGA.

## What the smoke probe demonstrates

Boot output on the VGA buffer:

```
Hello world!
test
km1=01000000 km2=01001000 km3=01000000
```

- `km1 = 0x01000000`: first allocation lands at the start of the heap pool.
- `km2 = 0x01001000`: 100 bytes from `km1` rounds up to 1 block, so `km2` starts one block (4 KiB = 0x1000) later.
- `km3 = 0x01000000`: after `kfree(p1)` the table marks that block free, and the next allocation of similar size reuses it.

This proves three things at once:
1. The block-to-address math is correct.
2. The TAKEN bit is being set and cleared.
3. The linear scan in `heap_get_start_block` finds the freed slot.

## How the test verifies it

`tests/13-kmalloc-works.sh` boots the image and grep the VGA buffer for the three expected hex strings. If any of `km1=01000000`, `km2=01001000`, or `km3=01000000` is missing, the test fails.

## Quirks worth noting

- **Each `kmalloc` rounds up to a full 4 KiB block.** Asking for 100 bytes consumes 4 KiB.
- **No defragmentation, no compaction.** Free a block, allocate from it; the order matches allocation order modulo what gets freed.
- **No alignment guarantees beyond 4 KiB.** Always page-aligned because of the block design.
- **No locking.** Single CPU only.
- **`heap_create` validates that the table size matches the pool size.** If `(end - ptr) / BLOCK_SIZE` does not equal `table->total`, `kheap_init` would print "Failed to create heap" and we continue with a broken kernel heap. Currently the constants are picked so they agree exactly.
- **The kmalloc smoke probe stays in `kernel_main` for now.** It is the simplest way to give the test something to grep. A future refactor will move it out once we have a proper test framework. The book leaves it inside `kernel_main` for this chapter too.

## What's next

Ch 49 adds C-callable `enable_interrupts` / `disable_interrupts` wrappers and moves the `sti` from kernel.asm into `kernel_main`. Then Ch 50-ish kicks off paging which finally turns the 100 MiB heap pool into actually-mapped memory.
