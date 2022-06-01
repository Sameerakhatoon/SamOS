# SamOs

A 64-bit multi-threaded kernel built from scratch, chapter by chapter, following Daniel McCarthy's **"Developing A Multi-Threaded Kernel From Scratch"** (Dragon Zap).

## How this repo is organized

```
samos/
├── README.md              # this file
├── PROGRESS.md            # checkbox per book chapter, commit hash next to each
├── docs/
│   ├── chapters/Chxx.md   # one note per book chapter (intent, what was built)
│   └── gotchas/Gxx.md     # one note per discovered bug + its fix
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

## Status

See [PROGRESS.md](PROGRESS.md).
