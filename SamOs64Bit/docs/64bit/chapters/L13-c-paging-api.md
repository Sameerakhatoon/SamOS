# Lecture 13 - restore + improve page mapping (Part 2)

**Source commit (PeachOS64BitCourse):** `d0508e0`
**SamOs commit:** L13 on `module1-64bit` branch
**Regression test:** `tests64/L13-c-paging.sh`

## Why this chapter exists

L12 set up the type vocabulary. L13 fills in the actual functions:
`paging_desc_new`, `paging_map`, `paging_map_range`, `paging_map_to`,
`paging_switch`, and the two asm leaves `paging_load_directory`
and `paging_invalidate_tlb_entry`. Together they let any caller
build a fresh 4-level page-table tree at runtime and load it into
CR3.

kernel_main exercises the whole chain end-to-end as a smoke test
(see "How we verified" below).

## Theory primer

### 1. Building a PML4 from scratch

`paging_desc_new(PAGING_MAP_LEVEL_4)`:
1. `kzalloc(struct paging_desc)` - the small header (pml pointer + level).
2. `paging_pml4_entries_new()` -> `kzalloc(struct paging_pml_entries)` - a 4 KiB block of 512 zero entries.
3. Wire the header to the table; return.

The result is a valid empty PML4. CR3 cannot be loaded with it yet
because PML4[0..511] are all zero (every walk would terminate at
"not present"). The map has to be populated first.

### 2. `paging_map(desc, virt, phys, flags)` - install one 4-KiB mapping

The 48-bit canonical virtual address is sliced into four 9-bit
indexes plus a 12-bit offset:

```
bits 47..39  PML4 index
bits 38..30  PDPT index
bits 29..21  PD index
bits 20..12  PT index
bits 11..0   offset within the 4 KiB page
```

The C code masks each index out:

```c
size_t pml4_index = (va >> 39) & 0x1FF;
size_t pdpt_index = (va >> 30) & 0x1FF;
size_t pd_index   = (va >> 21) & 0x1FF;
size_t pt_index   = (va >> 12) & 0x1FF;
```

Then walks downward, **kzalloc'ing missing intermediate tables on
demand**:

```c
if (pml4_entry is null)
    new_pdpt = kzalloc(512 entries);
    pml4_entry.address = new_pdpt >> 12;
    pml4_entry.present = 1; .read_write = 1;

if (pdpt_entry is null)
    new_pd = kzalloc(512 entries);
    ...

if (pd_entry is null)
    new_pt = kzalloc(512 entries);
    ...

pt_entry.address = phys >> 12;
pt_entry.present / .read_write per flags
```

If a PT slot was already mapped to something else, invalidate the
TLB entry for `virt` so the CPU doesn't serve the stale mapping
from cache.

This is the classic "lazy allocation" trick: a brand-new
descriptor uses zero memory for intermediate tables until the
first mapping in a given 1 GiB / 2 MiB region is installed.

### 3. Why `address` is `>> 12`

The 40-bit `address` field in our packed bit-field starts at bit
12 of the 8-byte entry. That position corresponds to bit 12 of the
physical address - i.e. the address is implicitly 4-KiB-aligned.
Whenever we write a 64-bit physical address into the field, we
shift right by 12 to drop the always-zero low bits.

When the CPU walks the entry it does the reverse: takes the 40-bit
field, shifts left by 12, and that's the next-table-base (or
page-base at the leaf). So `entry.address = (uintptr_t)thing >> 12`
on write, `((uintptr_t)entry.address) << 12` on read. Both sides
of paging.c do this.

### 4. `paging_switch(desc)` - load CR3

```c
void paging_switch(struct paging_desc* desc) {
    current_paging_desc = desc;
    paging_load_directory((uintptr_t*)(&desc->pml->entries[0]));
}
```

`paging_load_directory` is asm in `paging.asm`:

```nasm
paging_load_directory:
    mov rax, rdi    ; AMD64 SysV first arg is RDI
    mov cr3, rax    ; CR3 = arg
    ret
```

After this returns, every memory access goes through the new
4-level walk. The boot-time PD (in kernel.asm) is no longer
authoritative - only the new PML4 we just installed.

If the new PML4 doesn't map an address that the very next
instruction needs (e.g. RIP itself, or the stack), the CPU
`#PF`s and we lose. That's why L13's `kernel_main` does
`paging_map_range(desc, 0, 0, 102400, RW|P)` - identity-maps the
first 100 x 1024 x 4 KiB ≈ 400 MiB before the switch.

### 5. `invlpg` and the TLB

x86_64's TLB (Translation Lookaside Buffer) caches recent
virtual->physical translations per core. If we overwrite a PT
entry that's been touched, the next load through the old virtual
address could still return the cached physical mapping.

`invlpg [rdi]` evicts the TLB line that covers the supplied
virtual address. For a fresh mapping we don't bother - the line
was never cached. For a remapped slot we MUST invalidate.

The L13 `paging_map` only calls `paging_invalidate_tlb_entry` if
the PT slot was non-null on entry. For a clean descriptor that
walks zero entries, we never invlpg.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/paging/paging.c` | rewritten - `paging_desc_new`, `paging_map`, `paging_map_range`, `paging_map_to`, `paging_switch`, `paging_align_address`, `paging_align_to_lower_page`, `paging_pml4_entries_new`. All using `uintptr_t` for the void*↔int conversions (forward-fix for what would have been the same gotcha as L10's heap.c). |
| `src/memory/paging/paging.h` | adds the prototypes for the new C functions and the two asm helpers. The L12 types stay. |
| `src/memory/paging/paging.asm` | new 64-bit file - `paging_load_directory` (mov rax, rdi; mov cr3, rax; ret) and `paging_invalidate_tlb_entry` (invlpg [rdi]; ret). Was a 32-bit file with `push ebp` etc. |
| `src/kernel.asm` | PD_Table reverts from `%rep 20` of 4-KiB pages (L12) to `%rep 512` of 2-MiB PS=1 leaves (= 1 GiB coverage). The C-side mapper now owns the per-process and per-region map; the boot-time identity is only there to get us to `kernel_main`. |
| `src/kernel.c` | re-enables the L10 kheap probe (heap at 16 MiB is back in the boot map). Then adds the C-paging probe: `paging_desc_new(PAGING_MAP_LEVEL_4)`, `paging_map_range(desc, 0, 0, 102400, RW|P)`, `paging_switch(desc)`, `data[0]='M'`, `print(data)`. |
| `Makefile` | `FILES +=` `./build/memory/paging/paging.o`, `./build/memory/paging/paging.asm.o`. Rules added. |
| `build.sh` | `mkdir -p build/memory/paging`. |

## How we verified

`tests64/L13-c-paging.sh` boots, dumps VGA, asserts three
substrings:

| Expected | Proves |
|---|---|
| `Hello 64-bit!` | kernel.asm got us to kernel_main |
| `ABC` | kheap_init survived, kmalloc returned a writeable pointer, the first print round-tripped |
| `MBC` | paging_desc_new + paging_map_range + paging_switch worked; the kmalloc'd buffer is still readable AND writeable through the brand-new PML4 |

Observed:

```
Hello 64-bit!
ABCMBC
```

All three substrings present. Note that the data buffer at
~0x01000000 (kheap range) is reachable through the brand-new map
ONLY because `paging_map_range(... 1024 * 100, ...)` covered the
heap. If the count were too low, `data[0] = 'M'` would `#PF`
between the print calls and we'd see `ABC` but not `MBC`.

## Forward-look bugs we did NOT hit

The 32-bit `paging.c` had this same line that broke `heap.c` at L10:

```c
if (((unsigned int)virt % PAGING_PAGE_SIZE) || ((unsigned int) phys % PAGING_PAGE_SIZE))
```

L13 rewrites the whole file from scratch using `uintptr_t`
throughout, so the pointer-truncation bug doesn't have a chance
to fire. Worth noting that the 32-bit branch's `paging.c` is now
permanently archived in main branch history.

## Next lecture

Lecture 14 - restructure build files. The Makefile has been growing
linearly (one rule per .c file). PeachOS64 introduces a pattern-based
build at L14 so future lectures don't keep adding boilerplate.
