# Ch 5 - Project setup

**Code added:** `src/boot.asm` (empty), `build.sh` (stub)

## What upstream says

- Create a project directory (upstream calls it `PeachOS`; we call ours `SamOs`).
- Inside, create `boot.asm` - first source file, will hold the bootloader.
- Open it in an editor.

## What we add

- `src/boot.asm` - empty placeholder; the next chapter ("The Hello World Bootloader") fills it.
- `build.sh` - top-level build script. Currently a stub; the next chapter wires `nasm` into it.

Why `src/` instead of repo root: SamOs uses `src/` to separate kernel sources from user programs (`programs/`) and tests (`tests/`). The upstream repo flattens everything at the root; we don't.
