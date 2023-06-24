# Lecture 31 - paging_get + missing paging functions

**Source commit (PeachOS64BitCourse):** `6ba8bc8`
**SamOs commit:** L31 on `module1-64bit` branch
**Regression test:** `tests64/L31-paging-get.sh`

## Why this chapter exists

L29 introduced `paging_get_physical_address` for `multiheap_free`'s
virtual-arena branch. The walk it relied on, `paging_get`, was
stubbed to NULL. L31 implements the real walk.

## Theory primer: the L31 walk

```c
va = virt
pml4_idx = (va >> 39) & 0x1FF
pdpt_idx = (va >> 30) & 0x1FF
pd_idx   = (va >> 21) & 0x1FF
pt_idx   = (va >> 12) & 0x1FF

pml4_entry = &desc->pml->entries[pml4_idx]
if null(pml4_entry): return NULL
pdpt = (pml4_entry.address << 12)
pdpt_entry = &pdpt[pdpt_idx]
if null(pdpt_entry): return NULL
pd = (pdpt_entry.address << 12)
pd_entry = &pd[pd_idx]
if null(pd_entry): return NULL
pt = (pd_entry.address << 12)
return &pt[pt_idx]    // may be null; caller checks
```

Identical structure to `paging_map`'s downward walk, minus the
"create missing intermediate tables" branches. The returned PT
entry is "raw" - the caller decides what to make of a null entry
(e.g. paging_get_physical_address returns NULL when the leaf is
not present).

## Upstream's cut-paste bugs

Upstream's L31 implementation has three places where the
intermediate null check tests the wrong variable:

```c
struct paging_desc_entry* pdpt_entries = ...;
if (paging_null_entry(pdpt_entries))   // <-- should be &pdpt_entries[pdpt_idx]
    return NULL;
struct paging_desc_entry* pdpt_entry = &pdpt_entries[pdpt_index];
if (paging_null_entry(pdpt_entries))   // <-- duplicate, same bug
    return NULL;
```

The bug doesn't immediately bite: if `pml4_entry` was non-null,
`pdpt_entries` points at a real allocated PDPT (a 4 KiB zeroed
table). `paging_null_entry(pdpt_entries)` compares the FIRST 8
bytes of that table against a zero struct - so it returns true
iff `PDPT[0]` is unmapped. PDPT[0] being unmapped is fine - we
might be indexing PDPT[5] - but the check spuriously bails the
walk in that case.

SamOs writes the clean version (`null(pdpt_entry)`, not
`null(pdpt_entries)`). The L31 doc here calls out the deviation.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/paging/paging.c` | `paging_get` STUB replaced with the real PML4 -> PT walk. A static forward decl for `paging_null_entry` is added at the top of the file so `paging_get` (hoisted to the top with L27 - L29 helpers) can use it. |
| `src/memory/paging/paging.h` | comment on the prototype updated. |

## How we verified

VGA after L31:

```
Hello 64-bit!
e820 total: 267910144
ABCMemory wasted
```

Same tokens. `paging_get` is wired but `multiheap_free`
(which is its only caller in our tree) is still dormant
(`kfree` is a no-op).

## Forward look

L32 calls `multiheap_ready` from `kernel_main` so the virtual
arena gets stood up at boot. Post that, the IO/IDT/keyboard/
process restoration arc begins.
