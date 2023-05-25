# Lecture 49 - GDT entry + TSS descriptor C builders

**Source commit (PeachOS64BitCourse):** `8306baf`
**SamOs commit:** L49 on `module1-64bit` branch
**Regression test:** `tests64/L49-gdt-tss-c.sh`

## Why this chapter exists

The 32-bit GDT bring-up went through `struct gdt_structured`
(base, limit, type) and an `encodeGdtEntry` byte-packer. The
64-bit equivalent is direct: callers fill in a struct field by
field via `gdt_set`. The TSS descriptor needs its own setter
because the long-mode TSS is 16 bytes (two GDT slots), not 8.

## Theory primer: long-mode TSS descriptor layout

```
+0  limit0          16  bits 0..15 of the TSS structure's size
+2  base0           16  base bits 0..15
+4  base1            8  base bits 16..23
+5  type             8  access byte (P/DPL/S/type)
+6  limit1_flags     8  bits 0..3 = limit 16..19, bits 4..7 = flags
+7  base2            8  base bits 24..31
+8  base3           32  base bits 32..63        <-- the long-mode extension
+12 reserved        32  must be zero
```

The classic 8-byte GDT entry stops at byte 7. Long-mode TSS
descriptors keep going: 4 more bytes of base (so the TSS can
live anywhere in the 64-bit address space) and 4 reserved zero
bytes. Hence "TSS occupies two GDT slots".

`gdt_set_tss` splits the 64-bit base across base0/base1/base2/
base3 explicitly. The reserved field is zeroed (memset at the
top + explicit `desc->reserved = 0` for clarity).

## Theory primer: TSS_DESCRIPTOR_TYPE

The access byte for the TSS is `0x89`:
- P=1 (present)
- DPL=00 (ring 0)
- S=0 (system descriptor)
- type=1001 (available 64-bit TSS)

`= 1000 1001 = 0x89`. Defined as `TSS_DESCRIPTOR_TYPE` in
config.h so the L51 setup site doesn't need to re-derive the
bit layout.

## What's gone

- `struct gdt_structured` removed.
- `encodeGdtEntry` removed.
- `gdt_structured_to_gdt` removed.
- `gdt_load` (the asm-side loader) prototype removed from
  gdt.h. The loader still exists in gdt.asm but L49 doesn't
  expose it - it'll come back when L51 installs the GDT at
  runtime.

## SamOs deviation

Upstream's gdt_set_tss assigns `flags` to `limit1_flags`'s
upper nibble directly with `= (uint8_t)((limit >> 16) & 0x0F)`,
ignoring the `flags` arg entirely. That's a bug - the caller's
flags get dropped. SamOs ORs the flags in:

```c
desc->limit1_flags = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
```

Upstream may fix later or leave dead. Either way the call site
in L51 typically passes `flags = 0` so the bug is silent.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/gdt/gdt.h` | rename struct gdt -> struct gdt_entry. Add struct tss_desc_64 (16 bytes). gdt_set + gdt_set_tss prototypes. Remove gdt_structured / gdt_structured_to_gdt / gdt_load. |
| `src/gdt/gdt.c` | implementations. memset.h pulled in for the tss memset. |
| `src/config.h` | TSS_DESCRIPTOR_TYPE = 0x89. |
| `Makefile` | adds gdt/gdt.o to FILES + rule. |
| `build.sh` | mkdir build/gdt. |

## How we verified

VGA tokens unchanged from L38 - L48. gdt.o links cleanly into
the build. Nothing calls `gdt_set` / `gdt_set_tss` yet; L50
allocates and initialises a TSS, L51 installs it.

## Forward look

L50 - the TSS is allocated, configured, and `tss_load`'d. L51
adds the GDT-side TSS descriptor at runtime (via gdt_set_tss).
L52 - L53 restores keyboard. L54+ fills in isr80h commands.
