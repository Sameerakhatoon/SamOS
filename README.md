# SamOs

A 64-bit multi-threaded kernel built from scratch, chapter by chapter, following Daniel McCarthy's **"Developing A Multi-Threaded Kernel From Scratch"** (Dragon Zap).

## How this repo is organized

```
samos/
├── README.md              # this file
├── src/                   # kernel sources
├── programs/              # user-space utilities (built once the book gets there)
└── tests/                 # end-to-end tests (bash; one script per feature)
```

## Build & run

Toolchain is the GCC cross-compiler at `$HOME/opt/cross/` (`i686-elf-gcc` early; `x86_64-elf-gcc` after we switch to 64-bit), plus `nasm` and `qemu-system-x86_64`.

```bash
./build.sh
qemu-system-x86_64 -hda bin/os.bin -m 512 -accel tcg -display none -serial stdio
./tests/run.sh
```

## Notes

The study notes written along the way (one per book chapter and one per
bug found) are kept with the book that goes with this repository.
