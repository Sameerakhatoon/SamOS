# Ch 121 - ELF loader part 1 (elf.h)

Lays down `src/loader/formats/elf.h`. Header-only - nothing yet links into the build because no .c file includes it. The point of this chapter is to commit the on-disk ELF layout so the upcoming loader chapters have shared types to work with.

## What landed

- Constants:
  - `PF_X/W/R` - program-header permission flags
  - `PT_NULL/LOAD/DYNAMIC/INTERP/NOTE/SHLIB/PHDR` - program-header types
  - `SHT_NULL..SHT_HIUSER` - section-header types
  - `ET_NONE/REL/EXEC/DYN/CORE` - file types
  - `EI_NIDENT/CLASS/DATA`, `ELFCLASSNONE/32/64`, `ELFDATANONE/2LSB/2MSB`, `SHN_UNDEF`
- Typedefs: `elf32_half`, `elf32_word`, `elf32_sword`, `elf32_addr`, `elf32_off`
- Structs (all `__attribute__((packed))`):
  - `elf32_phdr` - program header
  - `elf32_shdr` - section header
  - `elf_header` - file header
  - `elf32_dyn` - dynamic table entry (union of value/pointer)
  - `elf32_sym` - symbol table entry

Each struct mirrors the on-disk layout of an ELF32 little-endian file. Pointer-casting an in-memory buffer to these types is the loader's main move.

## Test impact

None. No .c uses these yet. Build is unchanged.
