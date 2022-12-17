# Ch 122 - ELF loader part 2 (helpers, validation, struct elf_file)

Lays down the .c files that turn the header-only Ch 121 work into something usable. Nothing in the kernel calls these yet - Ch 124+ wires them into process_load.

## What landed

- `elf.h` now exports `elf_get_entry_ptr` and `elf_get_entry`.
- `elf.c` (new): `elf_get_entry` / `elf_get_entry_ptr` - one-liner getters for `e_entry`. Pulled into their own translation unit because the loader-side helpers in `elfloader.c` will keep depending on heavier headers (paging, status, file).
- `elfloader.h` (new): defines `struct elf_file { char filename[SAMOS_MAX_PATH]; int in_memory_size; void* elf_memory; void* virtual_base_address; void* virtual_end_address; void* physical_base_address; void* physical_end_address; }`. Prototypes for every helper below.
- `elfloader.c` (new):
  - `elf_signature[] = { 0x7F, 'E', 'L', 'F' }` (file-scope).
  - Static validators: `elf_valid_signature`, `elf_valid_class` (accepts NONE or 32), `elf_valid_encoding` (accepts NONE or 2LSB), `elf_is_executable` (ET_EXEC + entry >= SAMOS_PROGRAM_VIRTUAL_ADDRESS), `elf_has_program_header` (e_phoff != 0).
  - Accessors: `elf_memory`, `elf_header`, `elf_sheader`, `elf_pheader`, `elf_program_header(idx)`, `elf_section(idx)`, `elf_str_table` (reads `e_shstrndx` -> section -> sh_offset).
  - `elf_validate_loaded` chains signature + class + encoding + has-program-header and returns SAMOS_ALL_OK or -EINVARG.
- `Makefile` + `build.sh` build the two new objects under `build/loader/formats/`.

## Note on PEACHOS -> SAMOS

The book references `PEACHOS_PROGRAM_VIRTUAL_ADDRESS` and `PEACHOS_MAX_PATH`. Our equivalents are `SAMOS_PROGRAM_VIRTUAL_ADDRESS` (in config.h) and `SAMOS_MAX_PATH` (in kernel.h). `elfloader.h` therefore pulls in both kernel.h and config.h.

## Test impact

None. No new functionality is reachable from kernel_main yet.
