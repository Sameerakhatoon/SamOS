# Ch 35 - Addressing Alignment Issues

**Book pages:** 98+ (Part 5)
**Code updated:** `src/linker.ld`
**Test:** all 5 tests still pass

## What changed

Each output section in `linker.ld` now has `ALIGN(4096)`:

```ld
.text : ALIGN(4096) { *(.text) }
.rodata : ALIGN(4096) { *(.rodata) }
.data : ALIGN(4096) { *(.data) }
.bss : ALIGN(4096) { *(COMMON) *(.bss) }
```

The linker rounds up the current location counter to the next 4 KiB boundary before laying down the section.

## Why 4096

That is the x86 page size. When we turn on paging (a few chapters from now), pages map at 4 KiB granularity. Page table entries control read/write/execute and user/supervisor protection per page. If `.text` and `.rodata` share a page we cannot mark one read-only-executable and the other read-only-no-execute. Aligning each section to its own page boundary lets us assign different protection bits later.

Even before paging, alignment matters for:

- Code prefetchers, which work in cache-line-sized chunks (64 bytes on most modern CPUs).
- Some instructions like `movdqa` which require 16-byte aligned operands.
- Future ABI changes (some assembly conventions assume 16-byte stack alignment at function entry, for example).

## The effect on bin/kernel.bin size

With alignment, sections get padded so the next one starts on a page boundary. For our tiny kernel.asm stub (a handful of instructions in `.text`, nothing else), the file size only changes when there is content in `.rodata` / `.data` / `.bss` to align after. Right now those sections are empty so the binary stays roughly the same size. As we add C code the alignment shows up as zero padding between sections.

## Test impact

Behavior is unchanged. The boot sector loads the same bytes into the same physical addresses; the kernel still starts execution at `_start` (the first byte of `.text`). All 5 tests still pass.
