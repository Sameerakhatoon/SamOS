# Ch 26 - Automated Building with Makefiles

**Book pages:** 63-65 (Part 5)
**Code added:** `Makefile`
**Test:** unchanged behavior; `tests/05-enters-protected-mode.sh` still green

## What we added

A two-target Makefile at the repo root:

```makefile
all:
	nasm -f bin ./src/boot/boot.asm -o ./bin/boot.bin

clean:
	rm -rf ./bin/boot.bin
```

Indentation is hard tabs, not spaces (Make rejects spaces in recipe lines).

`make` (no argument) runs the first target, `all`. `make clean` deletes `bin/boot.bin` so the next `make` rebuilds from source.

## build.sh now delegates to make

The earlier `build.sh` directly called `nasm`. It now just runs `make` so there is one source of truth for build commands. Tests still call `./build.sh` so the test harness does not need to know whether we are on make, ninja, or anything else.

## Why the boot artifact is now bin/boot.bin

Previous chapters used `bin/os.bin` as the boot artifact because we were concatenating two files (boot.bin + data.bin). Since Ch 22 we have a single 512-byte image and the book consistently refers to it as `boot.bin`. The Makefile follows the book. `tests/05-enters-protected-mode.sh` was updated to boot `bin/boot.bin` instead of `bin/os.bin`.

When we start producing a real kernel image (a later chapter), the `os.bin` name will come back as the concatenation of boot sector plus kernel image, and Makefile rules will be added to assemble it.

## Why not just keep build.sh

We could. Make has one practical advantage at the scale we are heading: it skips work when timestamps say nothing changed. For a 512-byte boot sector that does not matter. Once we have dozens of `.c` and `.asm` files compiling into `.o` files and linking into a kernel, incremental builds save real time. Wiring make up now means we do not have to rewrite the build system later.
