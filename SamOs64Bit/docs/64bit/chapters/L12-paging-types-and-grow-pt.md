# Lecture 12 - restore + improve page mapping (Part 1)

**Source commit (PeachOS64BitCourse):** `09bc377`
**SamOs commit:** L12 on `module1-64bit` branch
**Regression test:** `tests64/L12-paging-types.sh`
**Cross-commit borrow:** boot.asm sector bump (PeachOS64's L34) and a SamOs-specific PT trim - see "What we had to do differently"

## Why this chapter exists

Two unrelated-looking changes ride together in this lecture:

1. **Vocabulary.** `paging.h` defines the 64-bit page-entry bit-field
   (`paging_desc_entry`), the 512-entry container
   (`paging_pml_entries`), and the descriptor pair (`paging_desc =
   pml + level`). Pure types - no functions, no code change. They
   give us a single named layout that every later mapping function
   will return / consume.
2. **Coverage.** `kernel.asm`'s PD_Table grows from 1 entry (L11's
   2 MiB) to a `%rep N` of entries pointing into a single big
   PT_Table. The L11 kheap probe was commented out because the
   heap at 0x01000000 sat outside the 2 MiB window; with PD growing
   to cover ≥ 16 MiB, the heap is reachable again.

PeachOS64's L12 uses `%rep 100` for 200 MiB of coverage. We had to
trim that - see below.

## Theory primer

### 1. The new entry bit-field

The 4-level paging hierarchy uses 8-byte entries at every level
(PML4E / PDPTE / PDE / PTE). The bit positions are identical across
levels - only the meaning of the 40-bit "address" field changes
(next-table base in intermediate levels, page base at the leaf).

```c
struct paging_desc_entry {
    uint64_t present         : 1;  // Bit 0
    uint64_t read_write      : 1;  // Bit 1
    uint64_t user_supervisor : 1;  // Bit 2
    uint64_t pwt             : 1;  // Bit 3
    uint64_t pcd             : 1;  // Bit 4
    uint64_t accessed        : 1;  // Bit 5
    uint64_t ignored         : 1;  // Bit 6 (dirty in PTE)
    uint64_t reserved0       : 1;  // Bit 7 (PS in PDE, reserved elsewhere)
    uint64_t reserved1       : 4;  // 8..11
    uint64_t address         : 40; // 12..51
    uint64_t available       : 11; // 52..62
    uint64_t execute_disable : 1;  // 63
} __attribute__((packed));
```

Why a packed bit-field instead of `uint64_t` plus macros?
- Reading `entry.present` is clearer than `(entry & 1)`
- The compiler can encode constants directly: `entry.present = 1;
  entry.address = phys >> 12;` becomes one OR instruction
- 32-bit SamOs used `uint32_t* directory` with helper macros; same
  destination, the bit-field is just nicer syntax

### 2. Why we have `paging_desc` AND `paging_pml_entries`

The kernel will eventually carve **per-process** address spaces. A
process needs:
- a pointer to its top-level table (PML4),
- and a hint about which level the top is at (4 today, 5 when
  LA57 / 5-level paging gets enabled).

`struct paging_desc { paging_pml_entries* pml; paging_map_level_t
level; }` is that pair. Future code will allocate one
`paging_desc` per process and load `pml` into CR3 when switching.

The 32-bit SamOs had `struct paging_4gb_chunk { uint32_t*
directory_entry; }` for the same role. Same idea, different word
size.

### 3. PD coverage math (post-L11)

L11 made PD entries point at PTs (PS=0). One PD entry × 512 PT
entries × 4 KiB = 2 MiB per PD entry. So `%rep N` of PD entries
covers `N × 2 MiB`.

PeachOS64's L12 uses `%rep 100` → 200 MiB.
SamOs's L12 uses `%rep 20` → 40 MiB. Reason: see next section.

### 4. Why we had to trim PD_ENTRY_COUNT from 100 to 20

Every PD entry needs 512 PT entries. Each PT entry is 8 bytes,
statically initialised. So PD_ENTRY_COUNT × 512 × 8 = bytes of
PT_Table in the **kernel binary** (`.text` section because we
don't move it).

| PD_ENTRY_COUNT | PT_Table size | Plus rest of kernel | Sectors needed |
|---|---|---|---|
| 100 (PeachOS64 L12) | ~400 KiB | ~430 KiB | **840+** |
| 30 | ~120 KiB | ~144 KiB | 281 |
| 20 | ~80 KiB | ~103 KiB | 202 |

`boot.asm` reads via ATA-1 LBA28, whose **sector-count register
is 8 bits** - max 255 sectors per command. PeachOS64's L34
("Loading more sectors") bumps from 100 → 250. That's their
ceiling.

Why don't they hit our problem? Their kernel.bin at L12 is
smaller because of the build files they don't compile yet vs. the
ones we already had ported from SamOs. We're effectively running
L12's asm with L10's kheap-objects build already in the link, so
our binary is bigger.

Two compatible fixes:
1. Trim `PD_ENTRY_COUNT` so the static PT_Table fits in 250
   sectors. **Picked this** - 20 entries (40 MiB) still covers
   the kheap range (16 MiB).
2. Bump boot.asm to issue multiple ATA commands (one per chunk
   of ≤ 255 sectors). I tried this first; my draft pushed the
   boot.asm beyond 510 bytes and broke the boot signature.
   PeachOS64 doesn't do this either, so the simpler fix wins.

L13 brings the C-side mapper online; the static `%rep` disappears
entirely there and the trim becomes moot.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/kernel.asm` | PD_Table: `%rep 1` (L11) → `%rep 20`; PT_Table: `%rep 512` (L11) → `%rep 512*20`. `PD_ENTRY_COUNT` named with a constant. |
| `src/memory/paging/paging.h` | Replaces the 32-bit prototypes with the L12 types: `paging_map_level_t`, `paging_desc_entry`, `paging_pml_entries`, `paging_desc`. Old 32-bit prototypes preserved as comments for cross-reference. |
| `src/memory/paging/paging.c` | The 32-bit body is dropped. File reduces to `#include "paging.h"` plus a forward-looking comment that L13 fills in the implementation. NOT in the Makefile FILES list yet. |
| `src/boot/boot.asm` | Sector count: `100` → `250` (PeachOS64's L34 fix landed early). Annotation explains why. |
| `Makefile`, `build.sh` | No change. paging.o is not yet in the link. |

## How we verified

`tests64/L12-paging-types.sh` builds, boots, asserts:

1. "Hello 64-bit!" still on VGA - the new 4-KiB walk through 20
   PD entries works end-to-end for the 0xB8000 VGA mapping.
2. Long-mode invariants intact: CS=0x18, CR0.PG, CR4.PAE, EFER.LMA.

Observed kernel.bin size: 103,384 bytes (= 202 sectors). Boot.asm
loads 250, comfortably above. The first 8 PD entries cover the
range that actually contains kernel + heap + VGA.

## What we had to do differently from PeachOS64

| What | PeachOS64 | SamOs L12 | Why |
|---|---|---|---|
| PD entry count | 100 | 20 | Their L12 kernel.bin is smaller; we run with all of L10's heap subsystem already linked, pushing our binary past their threshold. Both choices serve L12's pedagogical point (extend the static identity map). |
| boot.asm sectors | 100 (then 250 at their L34) | 250 (bumped now) | We hit the size wall at L12; they hit it later. Same fix, different timing. |

## Next lecture

Lecture 13 - restore + improve page mapping Part 2. Bring back
`paging.c` with real 64-bit `paging_set` / `paging_get` /
`paging_map_to` functions that walk the 4-level tree at runtime.
`kernel_main` will rebuild the identity map from C instead of
relying on the `%rep` block in kernel.asm, and we can drop
PD_ENTRY_COUNT entirely.
