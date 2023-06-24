# Ch 8 - Refining Our Bootloader

**Book pages:** 22-24 (Part 4)
**Code updated:** `src/boot.asm`
**Test:** still `tests/02-bootloader-prints-hello.sh` (behavior unchanged)

## What changed

The previous bootloader had `ORG 0x7c00` and relied on BIOS having sane segment registers when control was handed over. That assumption is fragile across BIOS implementations.

The refined version:

1. `ORG 0` instead of `ORG 0x7c00`. Labels are now offsets within the segment, not absolute physical addresses.
2. Sets segment registers explicitly: `DS = ES = 0x07C0`, `SS = 0x0000`.
3. Brackets the segment register setup with `cli` and `sti` so an interrupt cannot fire while segment registers are partway updated (which would corrupt the stack reference).

Putting it together:

```
DS = 0x07C0
[label]  ;= DS:label = 0x07C0 * 16 + label = 0x7C00 + label
```

So even though NASM emits offsets relative to 0, the runtime physical address resolves to 0x7C00 + offset, which is exactly what BIOS expects.

## Why we don't trust BIOS

The book's reason: different BIOS implementations set `CS:IP` to different combinations that produce the same physical 0x7C00. Some use `CS=0x0000, IP=0x7C00`, others use `CS=0x07C0, IP=0x0000`. The data and extra segments are even less consistent. Explicit setup removes the ambiguity.

## Quirk: SP is not initialized

The book sets `SS = 0` but never sets `SP`. SP retains whatever value BIOS left in it, which is usually safe but technically undefined behavior. The Hello World bootloader makes one `call` (which pushes a return address) and one `ret` (which pops it), so we get away with it. A future chapter or gotcha note may surface this.

## Why this still passes the same test

The visible behavior is identical: "Hello, World!" appears on screen. Only the way the bootloader sets itself up has changed. The existing `tests/02-bootloader-prints-hello.sh` covers the new code without modification.
