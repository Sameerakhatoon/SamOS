# Ch 125 - ELF loader part 5 (wire ELF into process_load, switch kernel to blank.elf)

The chapter that flips the switch. After this, `process_load_switch("0:/blank.elf", ...)` actually executes the ELF binary in ring 3.

## What landed

- `status.h`: new `EINFORMAT = 9` (returned when a file isn't an ELF).
- `elfloader.c::elf_validate_loaded` now returns `-EINFORMAT` instead of `-EINVARG`. `process_load_data` (below) keys off this code to fall back to the flat-binary path.
- `paging.{h,c}` gains `paging_align_to_lower_page(addr)` - rounds an address DOWN to the previous page boundary. Used to map ELF segments whose `p_vaddr` isn't page-aligned. (Existing `paging_align_address` rounds UP - the two are mirror operations.)
- `process.h`:
  - `PROCESS_FILETYPE_ELF = 0`, `PROCESS_FILETYPE_BINARY = 1`, `typedef unsigned char PROCESS_FILETYPE`.
  - `struct process` adds `PROCESS_FILETYPE filetype` and turns the old `void* ptr` field into a `union { void* ptr; struct elf_file* elf_file; }`.
- `process.c`:
  - includes `loader/formats/elfloader.h`.
  - `process_load_binary` sets `process->filetype = PROCESS_FILETYPE_BINARY` before writing `ptr`/`size`.
  - new `process_load_elf` calls `elf_load`, stashes the result, sets filetype to ELF.
  - `process_load_data` tries ELF first; if it returns `-EINFORMAT` (signature didn't match), falls back to `process_load_binary`. So both raw `.bin` and `.elf` files work end-to-end.
  - new `process_map_elf` maps the ELF segment range: `paging_map_to(... paging_align_to_lower_page(elf_virtual_base) ... elf_phys_base ... paging_align_address(elf_phys_end) ..., PRESENT|ACCESS_FROM_ALL|WRITEABLE)`.
  - `process_map_memory` becomes a switch on `filetype` (ELF -> `process_map_elf`, BINARY -> `process_map_binary`, default -> `panic`). The stack mapping stays after the switch, identical for both types.
- `kernel.c`: `process_load_switch` now targets `"0:/blank.elf"`. The flat `blank.bin` is still in the FAT and will still load if asked (the dual-path produced in Ch 123 stays for now).

## Verifying

All 32 tests still pass. Tests 38/39 still send a key and see putchar output - now driven by the ELF-loaded blank, not the flat binary.

## Known gap (book mentions it explicitly)

The mapping makes the whole ELF range writeable. Read-only `.rodata` isn't enforced yet. A later chapter walks program headers individually and respects `p_flags`.
