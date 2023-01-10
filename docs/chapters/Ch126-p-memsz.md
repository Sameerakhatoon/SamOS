# Ch 132 - p_filesz -> p_memsz in process_map_elf

One-line fix. `process_map_elf` was computing the upper bound of the mapping with `phdr->p_filesz`, which is the segment's size on disk. Some segments (notably `.bss`) have `p_filesz == 0` and `p_memsz > 0` because they're zero-fill at runtime - the mapper would silently skip mapping any pages, then the user code would page-fault the first time it touched `.bss`.

The fix: use `p_memsz` instead. That's the runtime memory footprint the ELF expects.

```diff
-                            paging_align_address(phdr_phys_address + phdr->p_filesz),
+                            paging_align_address(phdr_phys_address + phdr->p_memsz),
```

## Test impact

None visible - blank.elf currently has no `.bss`-only segment, so this is a forward-looking fix. The change matters as soon as a user program declares uninitialized globals.
