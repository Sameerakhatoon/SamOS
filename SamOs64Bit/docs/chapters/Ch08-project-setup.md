# Ch 5 - Project setup

**Book page:** 12 (Part 4 - Real Mode Development)
**Code added:** `src/boot.asm` (empty), `build.sh` (stub)

## What the book says

- Create a project directory (the book calls it `PeachOS`; we call ours `SamOs`).
- Inside, create `boot.asm` - first source file, will hold the bootloader.
- Open it in an editor.

## What we add

- `src/boot.asm` - empty placeholder; the next chapter ("The Hello World Bootloader") fills it.
- `build.sh` - top-level build script. Currently a stub; the next chapter wires `nasm` into it.

Why `src/` instead of repo root: SamOs uses `src/` to separate kernel sources from user programs (`programs/`) and tests (`tests/`). The book repo flattens everything at the root; we don't.

## Why this chapter exists

Bootstraps the project directory layout.

## How the change lands

mkdir + .gitkeep stubs in src/, bin/, build/.

## Regression test

build itself running proves layout works.

## Commit

Original landing: ch5 project setup (see `git log --oneline` for the actual hash on your branch).
