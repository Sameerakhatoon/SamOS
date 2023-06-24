# Lectures 50 + 51 - TSS allocated, init'd, installed

**Source commits (PeachOS64BitCourse):** `b9b049e` (L50), `687b0ba` (L51)
**SamOs commit:** L50-L51 on `module1-64bit` branch
**Regression test:** `tests64/L51-tss-installed.sh`

L50 wires the C-side TSS work; L51 reserves the GDT slots it
lands in. SamOs collapses them into one commit because L51 by
itself is content-free for us (we added the reserved slots in
L50 alongside the C-side write, so the L51 patch was already
applied).

## What changes

`kernel.asm`:

- two reserved-zero GDT slots appended after the user data
  seg (gdt[7] and gdt[8])
- `global gdt` so kernel.c can index into the static table

`kernel.c`:

- include `task/tss.h` and `gdt/gdt.h`
- declare `extern struct gdt_entry gdt[]` (refers to the
  kernel.asm symbol)
- file-scope `struct tss tss`
- replace the L38 div_test smoke with the L50 TSS bring-up:
  - `kzalloc(1 MiB)` -> ring-0 stack
  - `paging_map(kernel_desc(), stack_low, stack_low, 0)` to
    mark the lowest page as a not-present GUARD
  - memset tss to zero, set rsp0 to the high end of the
    stack, set iopb_offset = sizeof(tss) (no IOPB)
  - `gdt_set_tss((struct tss_desc_64*)&gdt[7], &tss,
    sizeof(tss)-1, TSS_DESCRIPTOR_TYPE, 0)`
  - print "tss ready"

`tss.h`:

- struct tss widened to long-mode layout (rsp0/1/2, ist1..7,
  iopb_offset)

`tss.asm`:

- rewritten under [BITS 64], SysV arg in DI, `ltr ax; ret`

`config.h`:

- `KERNEL_LONG_MODE_CODE_GDT_INDEX = 3`
- `KERNEL_LONG_MODE_DATA_GDT_INDEX = 4`
- `KERNEL_LONG_MODE_TSS_GDT_INDEX = 7`

`Makefile`:

- add `build/task/tss.asm.o` to FILES; rule.

## Why we don't ltr yet

The GDTR was loaded at boot with `limit = gdt_end - gdt - 1`,
where `gdt_end` ended right after the user-data seg (slot 6).
The CPU's view of the GDT therefore stops at slot 6. Slots
7 + 8 exist in the data on disk but are OUTSIDE the CPU's GDT
bounds.

`ltr 0x38` (selector for slot 7) would fault on a #GP because
the selector is out of range.

To use the TSS we have to either:

1. Reload the GDTR with the larger limit
2. Or move the TSS install BEFORE the lgdt at boot

Upstream L51 does neither - they install the descriptor and
walk away. The TSS is therefore "installed but invisible".
Future lectures will need to fix this; SamOs notes it here for
when we get to that point.

## Guard page rationale

```c
paging_map(kernel_desc(), tss_stack_low, tss_stack_low, 0);
```

The L27 unmap idiom (paging_map with phys=NULL, flags=0 - well,
phys=stack_low + flags=0 here, which makes the entry present=0
either way). Marks the very bottom 4 KiB of the 1 MiB stack as
not-present. A stack overflow that walks past the bottom
triggers a #PF rather than silently corrupting the kheap blocks
below.

Cheap insurance against deep recursion bugs in interrupt
handlers.

## SamOs vs upstream

| Concern | Upstream | SamOs |
|---|---|---|
| Reserved GDT slots | added in L51 | added in L50 alongside C-side write |
| TSS installed but not loaded | yes | yes (documented) |
| gdt_set_tss flags arg | dropped | OR'd in (L49 fix carries through) |

## How we verified

VGA after L50+L51:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
tss ready
```

L38's "Divide by zero error" is gone - we removed the div_test
exercise. "tss ready" appears, proving:

- kzalloc(1 MiB) succeeded (multiheap had room)
- paging_map on the guard page returned cleanly
- gdt_set_tss wrote into gdt[7]/gdt[8] without faulting (those
  slots exist in the static asm-side GDT now)
- The print after all that proceeded normally

## Forward look

L52 - L53 restores keyboard + IRQ wiring. L54 fills in
isr80h. Until then the TSS sits there waiting to be `ltr`'d.
