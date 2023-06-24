# Ch 123 - ELF loader part 3 (blank program builds an ELF)

The user program turns into an ELF executable. The book wholesale replaces `blank.bin` with `blank.elf` here, then later wires the kernel's ELF loader to consume it.

## Deviation: ship both blank.bin and blank.elf

Following the book straight would delete `blank.bin` immediately, but `process_load_switch("0:/blank.bin", ...)` in kernel_main still expects the flat-binary path. The kernel doesn't learn to load ELFs until later chapters of the ELF arc. To keep the suite green in the meantime, we build BOTH artifacts:

- `programs/blank/linker.ld` - unchanged, still emits flat `blank.bin`.
- `programs/blank/linker-elf.ld` - NEW, identical sections but `OUTPUT_FORMAT(elf32-i386)` - emits `blank.elf`.
- `programs/blank/Makefile` - reworked: one `nasm` step produces `build/blank.o`, then two link steps produce `blank.bin` and `blank.elf`.
- Root `Makefile` - `mcopy`s both binaries into the FAT16 volume.

Once the kernel switches over to ELF loading (later in the ELF arc), `blank.bin` and its linker.ld will be deleted.

## Test impact

- `tests/26-fat16-resolver.sh` updated: the FAT root directory now has 3 entries (`hello.txt`, `blank.bin`, `blank.elf`) instead of 2. `rdt=00000003` is the new expectation.
- No other test changes; the kernel still loads `blank.bin` so the user-program tests behave identically.

## How to inspect the new ELF

```
sudo apt install pax-utils
dumpelf programs/blank/blank.elf
```

Or use `readelf -a` / `objdump -h` from any binutils install.
