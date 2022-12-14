# Ch 120 - Understanding ELF files (prose)

The book pauses code work to lay out what ELF is before the loader chapters begin in Ch 121. Highlights worth remembering when reading the upcoming code:

- **Signature**: bytes `0x7F 'E' 'L' 'F'` at offset 0 of any valid ELF file.
- **ELF header**: file's table of contents; says where program headers and section headers live, plus the entry-point virtual address.
- **Program headers** (`elf32_phdr`): used at load time. Describe segments to map into memory - `p_type == PT_LOAD` means "copy `p_filesz` bytes from `p_offset` into virtual address `p_vaddr`, zero-fill up to `p_memsz`". This is the only header type the kernel needs to parse to launch a static executable.
- **Section headers** (`elf32_shdr`): used at link/debug time. `.text`, `.data`, `.bss`, `.symtab`, `.strtab`. Most loaders ignore section headers for execution and only read program headers; we'll follow that convention.
- **String tables**: array of null-terminated strings. Symbol/section names are stored as offsets into a strtab, not embedded directly. Reduces redundancy.
- **Dynamic linking**: shared libs resolve at runtime via the dynamic table (`elf32_dyn`). The book mentions this for completeness; our kernel will only handle static binaries.
- **Identification**: `e_ident[EI_CLASS]` distinguishes 32-bit (`ELFCLASS32`) from 64-bit. Our kernel is 32-bit only.

## Why ELF over flat binaries

Today our `blank.bin` is a raw "code at virtual 0x400000" blob. ELF gives us per-segment permission flags, a `.bss` (zero-filled) section that doesn't bloat the file, and the freedom to put rodata/data wherever the linker thinks best. Once the ELF loader is in, user programs can grow real data sections without manual layout fights.

## Reference

ELF spec from the Linux Foundation: https://refspecs.linuxfoundation.org/elf/elf.pdf
