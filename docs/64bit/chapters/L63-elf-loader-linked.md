# Lecture 63 - re-enable ELF loading in the build

**Source commit (PeachOS64BitCourse):** `d3ba5e8`
**SamOs commit:** L63 on `module1-64bit` branch
**Regression test:** `tests64/L63-elfloader-linked.sh`

## Why this chapter exists

L42 stubbed `process_load_elf` and `process_map_elf` because
the loader files weren't in the build. L46 - L48 brought back
fs / disk / process binary loading. L62 produced 64-bit ELF
user binaries on disk.

L63 un-stubs the ELF paths and pulls `elf.c` + `elfloader.c`
into the build. The loader code itself is still ELF32-shaped -
it reads `e_phoff`, `e_shoff`, `p_vaddr` as 32-bit fields. Our
new BLANK.ELF / SHELL.ELF are ELF64, so the loader will
recognise them as "wrong class" via `EI_CLASS` and bail with
`-EINFORMAT`. `process_load_data` then falls through to
`process_load_binary` for the raw-bytes path.

L65 refactors the loader to ELF64. For now, L63 is the
"plumbing" step: the elf code compiles, links, and is reachable
- but doesn't actually run our ELF64 binaries yet.

## Changes

| File | Change |
|---|---|
| `Makefile` | adds `elf.o` + `elfloader.o` to FILES; build rules under `build/loader/formats/`. ELF programs now mcopy'd onto the disk image alongside SIMPLE.BIN. |
| `build.sh` | mkdir `build/loader build/loader/formats`. |
| `src/loader/formats/elf.c` | `(void*)e_entry` -> `(void*)(uintptr_t)e_entry` widening (32-bit field on 64-bit pointer). |
| `src/loader/formats/elfloader.c` | `(int)header + header->e_shoff` etc -> `(uintptr_t)header + ...`. p_vaddr casts likewise. `end_virtual_address` widened to `uintptr_t`. |
| `src/task/process.c` | re-include `loader/formats/elfloader.h`. Un-stub `process_load_elf` and `process_map_elf` (now matches upstream + L63's p_vaddr casts). |
| `src/kernel.c` | SamOs deviation: keep loading `SIMPLE.BIN` (not `BLANK.ELF`) until L65 refactor + ATA-PIO speedup. blank.elf at 2 MiB takes well over 30 seconds to PIO-read in QEMU TCG. |

## SamOs vs upstream

| Concern | Upstream | SamOs |
|---|---|---|
| kernel_main load target | `0:/blank.elf` | `0:/SIMPLE.BIN` (keeps test fast; L66 switches to ELF) |
| ELF programs on disk | mount + cp | mcopy onto os.bin |
| EI_CLASS check vs our ELF64 files | implicit fall-through | same; loader rejects, binary path takes over |

## How we verified

`tests64/L63-elfloader-linked.sh`:

1. `mdir bin/os.bin ::` lists SIMPLE.BIN, BLANK.ELF, SHELL.ELF.
2. `build/loader/formats/elf.o` and `elfloader.o` exist.
3. Kernel boots and reaches "user enter".

All pass.

## Forward look

L64 - filesystem / disk bug fixes (probably FAT16 dir lookup
corner cases).
L65 - elfloader refactored to ELF64 (struct elf64_phdr,
elf64_shdr, 64-bit e_entry/e_shoff/e_phoff).
L66 - first ELF program actually runs in long mode.
