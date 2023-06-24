# Ch 56 - Building on paging functionality

Book Ch 52: keep the identity map from Ch 51, but now expose the ability
to point any specific virtual page at any physical page. The book proves
this by aliasing two pointers onto the same 4 KiB heap block.

## What got added

- `src/memory/paging/paging.c`
  - `paging_is_aligned(addr)` - true when `addr % 4096 == 0`.
  - `paging_get_indexes(virt, &dir_idx, &tbl_idx)` - split a virtual
    address into its page-directory and page-table indices. Returns
    `-EINVARG` if the address is not page-aligned. Kept `static` because
    only `paging_set` needs it.
  - `paging_set(directory, virt, val)` - walk to the right page table
    via the directory and write `val` into the slot for `virt`. `val`
    already encodes phys-address + flags so callers control writability,
    presence, and access ring all in one word.
- `src/memory/paging/paging.h` - pulled in `<stdbool.h>`, added the
  prototypes for `paging_set` and `paging_is_aligned`. `paging_get_indexes`
  stays internal.
- `src/kernel.c` - just above `enable_paging()`, allocate one 4 KiB heap
  block with `kzalloc`, then `paging_set` the directory entry for virtual
  `0x1000` to point at it (with present + writable + ring-3 access).
  Just after `enable_paging()`, write `'A','B'` through a `(char*)0x1000`
  alias, then `print` both that alias and the original `ptr`. The screen
  prints `remap:ABAB` because the two virtual addresses now back onto
  the same physical bytes.

## Why `0xfffff000`

`paging_set` does `entry & 0xfffff000` to extract the page-table base
from a directory entry. The low 12 bits of a directory entry are flags
(present, writable, cache, etc.); masking them off leaves the
page-aligned physical address of the table. We then index that table by
`table_index` and overwrite the PTE.

## Heap usage knock-on for tests

Ch 51 already consumed 1025 heap blocks (1 directory + 1024 tables)
before user code ran. Ch 52's demo calls `kzalloc(4096)` once more
before `enable_paging`, taking the pre-`kernel_main`-probe block count
to 1026. So the `km1` smoke address shifted from `0x01402000` to
`0x01403000`. `tests/13-kmalloc-works.sh` bumped to match.

## How test 15 confirms the remap

`tests/15-paging-remap.sh` boots the kernel, lets it run through the
remap demo, then dumps VGA RAM and greps for `remap:ABAB`. If paging
remapping is broken in any way - the directory entry isn't found, the
PTE isn't written, the directory base mask is wrong - the second print
(via the original `ptr`) would still show two NULs (kzalloc'd buffer)
and the screen would show `remap:AB` instead of `remap:ABAB`.
