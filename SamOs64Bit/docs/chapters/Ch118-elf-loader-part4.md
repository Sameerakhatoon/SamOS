# Ch 124 - ELF loader part 4 (elf_load, process program headers)

This chapter adds the real loading and program-header walk. Nothing in `kernel_main` calls these yet; they're still self-contained.

## What landed

In `elfloader.h`:
- Accessor prototypes: `elf_virtual_base`, `elf_virtual_end`, `elf_phys_base`, `elf_phys_end`.
- Loader prototypes: `int elf_load(const char* filename, struct elf_file** file_out);` and `void elf_close(struct elf_file* file);`.

In `elfloader.c`:
- The four virtual/physical address accessors.
- `elf_process_phdr_pt_load(elf_file, phdr)` updates the running `virtual_base_address` (and corresponding physical base) to the lowest `p_vaddr` seen across PT_LOAD entries, and the `virtual_end_address` (and physical end) to the highest `p_vaddr + p_filesz`. Both fields start at NULL and a single PT_LOAD will set them; multi-segment binaries converge to the smallest base and largest end.
- `elf_process_pheader` is a switch on `phdr->p_type` - only PT_LOAD handled today, but the structure makes adding DYNAMIC / INTERP later mechanical.
- `elf_process_pheaders` walks `header->e_phnum` entries, calling `elf_process_pheader` for each. Stops on the first negative return.
- `elf_process_loaded(elf_file)` is the top-level driver: validate -> walk program headers.
- `elf_load(filename, file_out)`:
  1. `kzalloc(struct elf_file)`.
  2. `fopen(filename, "r")` -> fd.
  3. `fstat(fd)` for size.
  4. `kzalloc(filesize)` -> `elf_memory`.
  5. `fread(elf_memory, filesize, 1, fd)`.
  6. `elf_process_loaded`.
  7. `*file_out = elf_file` on success; `fclose(fd)` always.
- `elf_close(file)` `kfree`s the memory and the struct (NULL-safe).

## Book quirk fixed inline

The book's `elf_load` opens the file with `int res = fopen(...)` and then checks `if (res <= 0) goto out`. `fopen` returns 0 on failure (`fd = 0` is invalid). We mirror that check exactly but mapping `res = -EIO` on the failure path before goto so the caller sees a real errno instead of zero.

## Test impact

None. `elf_load` is callable but nothing calls it yet. Suite stays at 32/32.
