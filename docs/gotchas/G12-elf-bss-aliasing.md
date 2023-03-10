# G12 - .bss-only PHDR aliases the start of the ELF buffer

**Surfaced during:** post-Ch 137, while writing test 57 (BSS behavioural) which `static int bss_counter; bss_counter++;` from a user task.
**Fix:** detour PHDRs with `p_filesz < p_memsz` through a fresh `kzalloc(p_memsz)` buffer in `process_map_elf` (`src/task/process.c`); copy `p_filesz` bytes of file content into the front, map THAT buffer instead of `elf_buffer + p_offset`.
**Regression test:** `tests/57-bss-init.sh` asserts `BSS-OK` (counter incremented from 0 to 1) AND `BSS-FAIL` absent (counter did NOT start at the ELF magic 0x464C457F).

## Symptom

A static parked in user-space .bss reads back non-zero on its first
load and writes don't appear to persist properly across the rest of
the program. Concretely: `static int bss_counter; bss_counter++;
print(itoa(bss_counter));` prints `1179403647` (the little-endian
ELF magic `7F 45 4C 46` interpreted as int) on the first call rather
than `1`.

## Root cause

`process_map_elf` maps each program header with

```c
res = paging_map_to(directory,
                    paging_align_to_lower_page((void*)phdr->p_vaddr),
                    paging_align_to_lower_page(phdr_phys_address),
                    paging_align_address(phdr_phys_address + phdr->p_memsz),
                    flags);
```

and `phdr_phys_address` is computed as `elf_buffer + phdr->p_offset`.

For a .bss-only PHDR the file offset is zero: there is no actual
file content. The book uses `p_memsz` rather than `p_filesz` so the
mapping has the RIGHT SIZE, but its starting physical page is still
the very start of the ELF buffer in kheap (where `e_ident`, `e_type`,
`e_entry`, etc. live).

The result: user vaddr `0x403000` (the GCC-assigned start of .bss)
aliases the first bytes of the ELF file in memory. A user-mode read
of `bss_counter` returns the ELF magic. A user-mode write to it
clobbers the ELF header inside this process's loaded image.

For programs that never touch .bss this is invisible; for any
program that does, the bug is on a fast path.

## Fix

When `p_filesz < p_memsz`, allocate a fresh `kzalloc(p_memsz)` page
range, copy the `p_filesz` file bytes into the front (zero for pure
.bss; the head of a partial-data segment otherwise), and map THAT
buffer into user space instead of `elf_buffer + p_offset`:

```c
void* mapping_src = phdr_phys_address;
if(phdr->p_filesz < phdr->p_memsz){
    void* fresh = kzalloc(phdr->p_memsz);
    if(!fresh){
        res = -ENOMEM;
        break;
    }
    if(phdr->p_filesz > 0){
        memcpy(fresh, phdr_phys_address, phdr->p_filesz);
    }
    mapping_src = fresh;
}
res = paging_map_to(directory,
                    paging_align_to_lower_page((void*)phdr->p_vaddr),
                    paging_align_to_lower_page(mapping_src),
                    paging_align_address(mapping_src + phdr->p_memsz),
                    flags);
```

`kzalloc` clears the page, so the tail beyond `p_filesz` is the
zero-init `.bss` the user program expects.

The fresh kheap buffer leaks on process_terminate (no per-PHDR
tracking on the process struct) - acceptable tradeoff for the book's
single-pass loader, but worth a TODO.

## How it was found

Adding behavioural test 57 (a "BSS" branch in blank.c that does
`static int c; c++; if (c == 1) print("BSS-OK"); else print("BSS-FAIL")`).
With the book loader, every load printed `BSS-FAIL`. With the fix
above, every load prints `BSS-OK`.

## Related

- [[G04-iret-to-ring3-resets]] - process-side paging issue
- Ch 132 in the book - "process_map_elf uses p_memsz" only half-
  fixes the loader; G12 closes the other half.
