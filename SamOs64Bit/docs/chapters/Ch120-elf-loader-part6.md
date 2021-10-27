# Ch 126 - ELF loader part 6 (per-PHDR mapping with PF_W respect, ELF entry-point IP)

The final piece of the ELF arc. Two changes flip the loader from "good enough to launch" to "behaves like a real loader":

## Per-PHDR mapping

`process_map_elf` no longer maps the whole `[virtual_base, virtual_end]` range as one writable blob. It now walks every program header (`elf_pheader(header)`, `e_phnum` entries) and maps each PT_LOAD segment with the right permission bits:

- `PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL` always.
- `PAGING_IS_WRITEABLE` only when `phdr->p_flags & PF_W`.

So `.text` and `.rodata` ship as read-only; a buggy user program that scribbles over its own code segment now takes a page fault instead of silently corrupting itself.

Each segment uses `paging_align_to_lower_page` for the virt/phys base (the segment may start at a non-page-aligned `p_vaddr`) and `paging_align_address` to round the end up to the next page boundary.

`elfloader.{h,c}` gains `elf_phdr_phys_address(file, phdr) = elf_memory(file) + phdr->p_offset` so the mapper can ask "where in our loaded blob is THIS segment's data?".

## ELF entry-point IP

`task_init` (in `task/task.c`) previously hard-coded `task->registers.ip = SAMOS_PROGRAM_VIRTUAL_ADDRESS` (0x400000). For ELF processes, the actual entry point comes from `e_entry`, which may differ if the linker placed `_start` after rodata or wherever it pleased. After the change:

```c
task->registers.ip = SAMOS_PROGRAM_VIRTUAL_ADDRESS;
if (process->filetype == PROCESS_FILETYPE_ELF) {
    task->registers.ip = elf_header(process->elf_file)->e_entry;
}
```

Binary processes still get the static address (the flat-binary path has no header to read). For our current `blank.elf`, `e_entry` IS 0x400000 because the linker keeps `_start` first, so the behavior is identical. The change matters as soon as a user program has a non-trivial layout.

## Test impact

Still 32/32. The same getkey -> putchar pipeline runs under the new mapping; nothing in the suite specifically tests page-fault-on-rodata-write yet.
