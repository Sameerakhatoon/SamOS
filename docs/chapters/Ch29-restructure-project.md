# Ch 25 - Restructuring Our Project

**Book page:** 63 (Part 5)
**Change:** moved `src/boot.asm` to `src/boot/boot.asm`
**Test:** unchanged behavior; `tests/05-enters-protected-mode.sh` still green

## Why

As the project grows we will have multiple subsystems (boot, kernel, drivers, filesystem, libc, user programs). Putting each in its own subdirectory keeps the tree navigable.

The book's layout from this chapter:

```
PeachOS/
  build/
  bin/
  src/
    boot/
      boot.asm
```

SamOs already had `build/`, `bin/`, and `src/`. The only physical change here is sliding `boot.asm` one level deeper into `src/boot/`. `build.sh` updates the source path it passes to `nasm`.

## What is in build/

Nothing yet. The next chapter (Ch 26 Makefile) starts dropping intermediate object files in there. Until then it is just a placeholder directory.

## Test impact

The boot image bytes did not change so test 05 passes unchanged.
