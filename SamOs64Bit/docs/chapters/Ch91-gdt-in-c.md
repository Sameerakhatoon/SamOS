# Ch 91 - Transitioning kernel segments to C

Book Ch 89. We rewrite the bootloader's hand-assembled GDT as a C
data structure so the next chapters can add a TSS descriptor and
user code/data descriptors without touching boot.asm.

## What got added

- `src/config.h` - `SAMOS_TOTAL_GDT_SEGMENTS = 3` (NULL + kernel
  code + kernel data; user code/data + TSS will bump this later).
- `src/gdt/gdt.asm` - the asm half of GDT loading:
  - `gdt_load(struct gdt* gdt, int size)` - stashes the size into
    bytes 0..1 of `gdt_descriptor`, the start address into bytes
    2..5, and executes `lgdt [gdt_descriptor]`.
  - `gdt_descriptor` lives in `.data` as a 6-byte placeholder
    (size word + 32-bit base).
- `src/gdt/gdt.h`:
  - `struct gdt` - the on-CPU 8-byte descriptor layout, packed.
    Fields named per the book: `segment` (limit low 16), `base_first`
    (base low 16), `base` (base bits 16..23), `access` (type byte),
    `high_flags` (flags + limit bits 16..19), `base_24_31_bits`
    (base bits 24..31).
  - `struct gdt_structured` - human-readable form with `base`,
    `limit`, `type`.
  - `gdt_load(gdt, size)` and
    `gdt_structured_to_gdt(gdt, structured_gdt, total_entires)`
    prototypes (book typo `entires` preserved per convention).
- `src/gdt/gdt.c`:
  - `encodeGdtEntry(target, source)` - writes 8 raw bytes in the
    order the CPU expects. Sets the granularity nibble in `target[6]`
    to `0x40` for byte-granular limits, `0xC0` for page-granular
    (which also right-shifts the limit by 12).
    Panics via the Ch 82 `panic()` when given a limit that's
    between 64 KiB and 4 GiB and not page-aligned at the bottom.
  - `gdt_structured_to_gdt(gdt, structured_gdt, total)` - just calls
    `encodeGdtEntry` per slot.
- `Makefile` - `./build/gdt/gdt.o` + `./build/gdt/gdt.asm.o` added
  to `FILES`, plus the two compile rules.
- `build.sh` - `build/gdt` added to `mkdir -p`.
- `src/kernel.c`:
  - Includes `gdt/gdt.h` and `config.h`.
  - File-scope arrays:
    ```c
    struct gdt gdt_real[SAMOS_TOTAL_GDT_SEGMENTS];
    struct gdt_structured gdt_structured[SAMOS_TOTAL_GDT_SEGMENTS] = {
        { .base = 0x00, .limit = 0x00,        .type = 0x00 }, // NULL
        { .base = 0x00, .limit = 0xFFFFFFFF,  .type = 0x9A }, // Kernel code
        { .base = 0x00, .limit = 0xFFFFFFFF,  .type = 0x92 }  // Kernel data
    };
    ```
  - `kernel_main` now zeroes `gdt_real`, calls
    `gdt_structured_to_gdt` to populate it, then `gdt_load`s it -
    all before `kheap_init`. The new GDT mirrors the bootloader's
    layout (NULL at 0x00, kernel code at 0x08, kernel data at 0x10)
    so existing CS/DS/ES/SS values in the segment registers remain
    correct.

## Why the bootloader GDT is still there

We don't remove the GDT setup from `boot.asm`. It's needed to enter
protected mode in the first place; the C-side GDT is just a more
maintainable replacement that takes over after the kernel jumps in.
Removing the bootloader GDT now would leave nothing for `lgdt` to
inherit from when the CPU first flips PE.

## No new test

All 26 existing tests still pass, which means the kernel boots,
prints, opens files, reads them, etc. all with the C-side GDT
loaded. That's the regression-coverage we need for now; a focused
test ("read CS:DS via QEMU monitor and confirm they match the new
GDT") would tell us the same thing.

## What's next

- Ch 90 (book Ch 90): create the TSS struct, add it as the 4th GDT
  entry, add user code + user data as 5th and 6th, ltr the TR.
- After that, the IRET-into-userland trick to actually run ring-3
  code.
