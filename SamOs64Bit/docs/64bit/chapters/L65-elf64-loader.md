# Lecture 65 - refactor the ELF loader to ELF64

**Source commit (PeachOS64BitCourse):** `3ac2c1a`
**SamOs commit:** L65 on `module1-64bit` branch
**Regression test:** `tests64/L65-elf64-loader.sh`

## Why this chapter exists

L63 wired the ELF32 loader into the build. But our user
programs (since L62) are ELF64. The L63 loader rejected them on
the EI_CLASS check. L65 ports the loader to ELF64.

## Theory primer: ELF64 vs ELF32 differences

| Field | ELF32 | ELF64 |
|---|---|---|
| e_entry / addresses | 32-bit | 64-bit |
| e_phoff / e_shoff | 32-bit | 64-bit |
| sh_flags / sh_size etc | 32-bit | 64-bit |
| p_flags position (in elf*_phdr) | offset 24 (after p_align) | offset 4 (immediately after p_type) |
| struct sizes | elf32_phdr = 32B, elf32_shdr = 40B | elf64_phdr = 56B, elf64_shdr = 64B |

The `p_flags` reorder is the gotcha. In ELF32 the layout is
`{p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz,
p_flags, p_align}` - one 32-bit word each, totalling 32 bytes.
In ELF64 with 64-bit fields, putting `p_flags` between
`p_filesz` and `p_memsz` would leave the struct internally
unaligned (the 64-bit fields after a 32-bit p_flags would need
padding). Moving `p_flags` immediately after `p_type` keeps
everything 8-byte aligned without padding bytes.

If we got the order wrong, we'd misinterpret `p_offset` as the
flags (and vice versa) - the loader would either crash on a
garbage offset or apply wrong permissions to the segments.

## The .bss zero-fill

```c
if (total_size > filesize) {
    memset((char*)physical_base + filesize, 0, total_size - filesize);
}
```

The ELF file only stores the initialised portion of a segment.
`.bss` (zero-initialised data) is implicit - it's the gap from
`p_filesz` to `p_memsz`. The loader has to zero that range
explicitly when it stages the segment in memory; otherwise the
user program reads back whatever the kzalloc'd buffer happened
to contain.

kzalloc already returns zeroed memory in our heap, so the
memset is technically redundant TODAY. We do it anyway -
defense against future heap-recycling optimisations and to
match what real-world ELF loaders do.

## Type and struct renames

| Old | New |
|---|---|
| `elf32_addr` | `elf64_addr` (uint64_t) |
| `elf32_off`  | `elf64_off`  (uint64_t) |
| `elf32_half` | `elf64_half` (uint16_t, unchanged in width) |
| `elf32_word` | `elf64_word` (uint32_t, unchanged) |
| `elf32_xword`(none) | `elf64_xword` (uint64_t, new) |
| `struct elf32_phdr` | `struct elf64_phdr` (with reorder) |
| `struct elf32_shdr` | `struct elf64_shdr` |
| `struct elf32_dyn`  | `struct elf64_dyn` |
| `struct elf32_sym`  | `struct elf64_sym` (with field reorder) |
| `elf_get_entry(): uint32_t` | `elf_get_entry(): elf64_addr` |

The struct elf_header field types switch too: `e_entry` is now
`elf64_addr`, `e_phoff` is `elf64_off`, etc.

## What survived without changing

- elf_validate_loaded
- elf_load, elf_close
- The phdr iteration shape (elf_process_pheaders walks the
  table by index)
- process_load_elf in process.c

## Other touches

- `elf_valid_class` accepts `ELFCLASS64` (was 32)
- `task_init`'s ELF branch now sets
  `ip = elf_header(process->elf_file)->e_entry` (was a panic
  while the loader was stubbed)
- `process_free_elf_data` calls `elf_close` (was -1 stub)
- task.c re-includes `loader/formats/elfloader.h`

## How we verified

VGA after L65:

```
...
tss ready
Loading program...
user enter
```

The kernel still loads SIMPLE.BIN (the L66 commit will switch
the load target to BLANK.ELF). At L65 we just confirm the
loader code compiles cleanly under the ELF64 types and the
existing user-program path still works.

## Forward look

L66 - flip kernel.c's load target to `0:/BLANK.ELF`, run the
first ELF in long mode. Notes from the L63 SamOs deviation
chapter still apply: blank.elf is 2 MiB and may take a long
time to PIO-read in QEMU TCG. We may need to shrink the
linker script or strip unused sections.
