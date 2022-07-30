# Ch 34 - Improving Cleaning in the Makefile

**Book page:** 96-97 (Part 5)
**Code updated:** `Makefile`
**Test:** all 5 tests still pass after a `make clean && ./build.sh`

## What changed

The `clean` target now explicitly removes every intermediate the build produces:

```makefile
clean:
	rm -rf ./bin/boot.bin
	rm -rf ./bin/kernel.bin
	rm -rf ./bin/os.bin
	rm -rf ${FILES}
	rm -rf ./build/kernelfull.o
```

Before this chapter `clean` removed only `boot.bin`. After this chapter it also removes `kernel.bin`, `os.bin`, every entry in `FILES` (currently `./build/kernel.asm.o`), and `./build/kernelfull.o` (the relocatable intermediate produced by `i686-elf-ld`).

## Why it matters

Two reasons:

1. **Reproducible builds.** Without nuking the old intermediates, `make` decides not to rebuild because timestamps look fine. Most of the time that is what we want. When something goes wrong and we suspect a stale artifact, `make clean && ./build.sh` should restore a guaranteed-fresh state.
2. **Reviewability.** When the project tree is small, `ls bin/` after `make clean` should show nothing. Anything left behind tells us a build step is leaking files outside the clean target.

## Why `${FILES}` is used

`${FILES}` (also written `$(FILES)`) substitutes the contents of the `FILES` variable. Right now that's just `./build/kernel.asm.o`. As we add `.o` files for new kernel sources we just extend the variable, and `clean` automatically removes them all without any extra typing.
