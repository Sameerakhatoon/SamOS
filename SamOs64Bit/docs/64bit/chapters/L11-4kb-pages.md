# Lecture 11 - swap 2 MiB PS=1 leaves for 4 KiB PT entries

**Source commit (PeachOS64BitCourse):** `4b321e4`
**SamOs commit:** L11 on `module1-64bit` branch
**Regression test:** `tests64/L11-4kb-pages.sh`

## Why this chapter exists

Up to L10 every entry in our PD_Table was a **2 MiB leaf** -
`PS=1` told the CPU "stop the walk here, this entry's physical
address is the page base, the next 21 bits of the virtual address
are the page offset". 2 MiB leaves are great for the bootstrap
identity map (they cover huge ranges with very few entries) but
they are useless for any real allocator: there's no concept of a
"4 KiB free block" if your smallest mappable unit is 2 MiB.

The rest of the kernel - kheap (4 KiB blocks), the user-space
loader, the per-process address spaces we'll need from L40+ - all
assume 4 KiB pages. So L11 is the cutover: we add a Page Table
(PT) layer below the PD and shrink each leaf to 4 KiB.

The coverage drops dramatically as a side effect: from 130 MiB
(L10's `%rep 65` of 2 MiB pages) to a single 2 MiB window
(L11's `%rep 512` of 4 KiB pages). That's deliberate. The whole
point of L12 + L13 is to show how to extend the map back out
from C code instead of NASM macros.

## Theory primer

### 1. The PS bit decoded

The CPU walks page tables top-down. At each level it indexes
into the table with 9 bits of the virtual address, reads an
8-byte entry, and either:

- **continues** down (entry is a pointer to the next-level
  table), or
- **stops** here (entry IS the leaf - the physical-address base
  for this page).

The "stops here" path is signalled by the `PS` bit (bit 7) being
set IN AN INTERMEDIATE ENTRY. PS at the PD level says "this PD
entry is a 2 MiB leaf, don't walk down to a PT". PS at the PDPT
level says "this PDPT entry is a 1 GiB leaf, don't walk down to a
PD".

PS at the **PT level is reserved** - there's no level below PT,
so PS doesn't mean anything there. We MUST clear PS in PT
entries.

### 2. The L10 flags vs L11 flags

| Lecture | Where | PS_FLAG value | Meaning |
|---|---|---|---|
| L10 | PD entry | `0x83` = 1000_0011 | Present \| RW \| PS=1 (2 MiB leaf) |
| L11 | PD entry | `0x03` = 0000_0011 | Present \| RW \| PS=0 (next level is PT) |
| L11 | PT entry | `0x03` = 0000_0011 | Present \| RW \| (PS reserved, clear) |

Same `0x03` for the PD pointer-to-PT and for the PT 4-KiB leaves
- the bit pattern of the lowest byte does double duty. The CPU
disambiguates by **what level it's at**, not by the bit values.

### 3. Coverage math

L10: PML4 → PDPT → PD with 65 × 2 MiB = 130 MiB.
L11: PML4 → PDPT → PD → PT with 1 PD entry × 512 × 4 KiB = 2 MiB.

The single 2 MiB window covers:

- `0x00000000 .. 0x000003FF` BIOS interrupt vector table (unused)
- `0x00007E00`            our heap descriptor table (used by L10)
- `0x00100000`            where `_start` was loaded
- `0x00104000`            `kernel_main` (resolved earlier via nm)
- `0x000B8000`            VGA text buffer - what we use for the banner check
- `0x00200000`            the stack we point RSP at

Everything we need to BOOT is in that range. The first **outside**
address is the heap body at `0x01000000` - which is why we
commented out the L10 kmalloc probe in `kernel_main`. It would
`#PF` instead of returning a valid pointer.

### 4. Why we don't put PS=1 at PDPT for a 1 GiB page

A single 1 GiB PDPT leaf would cover 0 .. 0x3FFFFFFF (1 GiB)
in one entry - even more dramatic than 130 MiB of 2 MiB leaves.
But 1 GiB pages aren't supported on every CPU (need CPUID feature
flag `1GB_PAGES`), and we'd have to disable them again the moment
we start carving per-process address spaces. The kernel-side
allocator and the user-side page tables BOTH want 4 KiB
granularity, so the simpler path is to commit to 4 KiB now and
stay there.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/kernel.asm` | `PS_FLAG` constant: `0x83` → `0x03`. `PAGE_INCREMENT`: `0x200000` → `0x1000`. `PD_Table` was a `%rep 65` of 2 MiB leaves; now it's one entry pointing at `PT_Table`. New `PT_Table` label with `%rep 512` of 4 KiB leaves. |
| `src/kernel.c` | The L10 kheap probe (`kheap_init` + `kmalloc(50)` + `print(data)`) gets commented out with a note. `kernel_main` is back to `terminal_initialize` + `print("Hello 64-bit!\n")` + `while(1) {}`. |

The new asm layout:

```nasm
PML4_Table:
    dq PDPT_TABLE + 0x03
    times 511 dq 0

PDPT_TABLE:
    dq PD_Table + 0x03
    times 511 dq 0

PD_Table:
    dq PT_Table + 0x03      ; PD[0] -> PT_Table
    times 511 dq 0

PT_Table:
    %assign addr 0x00000000
    %rep 512
        dq addr + 0x03      ; 4-KiB page, Present | RW
        %assign addr addr + 0x1000
    %endrep
```

## How we verified

`tests64/L11-4kb-pages.sh`:

1. Builds the kernel.
2. Boots under QEMU TCG, dumps VGA (`pmemsave 0xb8000 4096`)
   AND `info registers`.
3. Asserts both:
   - "Hello 64-bit!" appears in the VGA buffer (so the 4-KiB
     walk reaches the VGA RAM at `0xB8000`)
   - Long-mode invariants still hold (CS=0x18, CR0.PG, CR4.PAE,
     EFER.LMA)

Observed: banner intact, all four invariants set. The 4-KiB walk
works.

## Things that broke and have to wait

- **Anything above 2 MiB.** kheap (`0x01000000`), shell.elf load
  address (`0x400000` user-space), task stacks. Don't try to
  touch any of them between L11 and the C-side mapper landing in
  L12+.
- **`paging.c`'s `(unsigned int)virt` truncation** - flagged at
  L10 - is still latent. It only fires when `paging.c` joins the
  build (sometime around L12).

## Next lecture

Lecture 12 - restore the C-side page mapping helpers. Bring back
`paging.c` and `paging.asm`, port them to long-mode page-table
formats (8-byte entries, 4 levels), add the `(unsigned int)` →
`(uintptr_t)` fix here too, and let `kernel_main` call
`paging_set` to extend the identity map back out to where the
kheap lives.
