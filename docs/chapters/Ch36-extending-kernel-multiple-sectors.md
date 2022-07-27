# Ch 32 - Extending Our 32-Bit Kernel Across Multiple Sectors

**Book pages:** 77-91 (Part 5)
**Code added:** `src/kernel.asm`, `src/linker.ld`, Makefile rewrite, `build.sh` sets cross-compiler PATH, `src/boot/boot.asm` learns to talk to ATA
**Test:** `tests/08-kernel-loaded.sh` (kernel reached) plus updated `tests/05` and `tests/06` now boot the full `os.bin`

## What we built

The boot sector now loads 100 sectors (50 KiB) starting at LBA 1 into physical address 0x100000 (1 MiB), then jumps there. The kernel (currently just a 32-bit asm stub) reloads segment selectors and spins.

The on-disk layout of `bin/os.bin`:

```
sector 0   (bytes 0..511)     : boot sector
sector 1+  (bytes 512..)      : kernel.bin contents (currently 23 bytes)
trailing                      : 100 sectors of zero padding
```

## src/boot/boot.asm changes

After the A20 enable, the bootloader sets up the read parameters and calls `ata_lba_read`:

```asm
mov eax, 1                  ; starting LBA (sector 1, the kernel)
mov ecx, 100                ; sector count
mov edi, 0x0100000          ; destination
call ata_lba_read
jmp CODE_SEG:0x0100000      ; jump into the loaded kernel
```

`ata_lba_read` is the textbook PIO read routine: send LBA bytes to ports 0x1F3-0x1F6, sector count to 0x1F2, READ command (0x20) to 0x1F7, then poll status until the DRQ bit (8) is set and `rep insw` 256 words per sector.

The far jump `jmp CODE_SEG:0x0100000` is important: a near jump would not reload CS, and the kernel's first instruction has to run under the same kernel code selector (0x08) we set up in the GDT.

## src/kernel.asm

The kernel image begins at offset 0 of sector 1, which the bootloader loads to physical 0x100000. The linker script (next section) plants the kernel's `.text` at exactly 1 MiB so addresses inside the kernel resolve correctly.

For now the kernel just reloads DS/ES/FS/GS/SS with the data selector and spins. We will add a C entry point next chapter.

## src/linker.ld

Tells `ld` to:

- Use `_start` (the symbol from `kernel.asm`) as the entry point.
- Output a flat binary (no ELF headers).
- Place `.text` at 1 MiB, followed by `.rodata`, `.data`, `.bss`.

`OUTPUT_FORMAT(binary)` is what produces a raw flat binary that the bootloader can copy directly into memory without parsing ELF.

## Makefile

```makefile
FILES = ./build/kernel.asm.o

all: ./bin/boot.bin ./bin/kernel.bin
	rm -rf ./bin/os.bin
	dd if=./bin/boot.bin >> ./bin/os.bin
	dd if=./bin/kernel.bin >> ./bin/os.bin
	dd if=/dev/zero bs=512 count=100 >> ./bin/os.bin

./bin/kernel.bin: $(FILES)
	i686-elf-ld -g -relocatable $(FILES) -o ./build/kernelfull.o
	i686-elf-gcc -T ./src/linker.ld -o ./bin/kernel.bin -ffreestanding -O0 -nostdlib ./build/kernelfull.o

./bin/boot.bin: ./src/boot/boot.asm
	nasm -f bin ./src/boot/boot.asm -o ./bin/boot.bin

./build/kernel.asm.o: ./src/kernel.asm
	nasm -f elf -g ./src/kernel.asm -o ./build/kernel.asm.o

clean:
	rm -rf ./bin/boot.bin ./bin/kernel.bin ./bin/os.bin ./build/*.o
```

Why we go through `i686-elf-ld -relocatable` then `i686-elf-gcc -T linker.ld`: the first step gathers all kernel `.o` files into one relocatable `kernelfull.o`. The second step applies the linker script to lay the sections out at 0x100000 and emit a flat binary. Going through GCC instead of LD lets us add `-ffreestanding -nostdlib` which suppresses the runtime startup code.

## build.sh

Now sets `PATH` to include `$HOME/opt/cross/bin/` so the Makefile can call `i686-elf-gcc` and `i686-elf-ld` by their bare names.

## Why the test now points at os.bin

Tests 05 and 06 previously booted `bin/boot.bin` directly. That worked when there was no kernel to load. Now the boot sector tries to read sector 1, and if we only hand it the 512-byte boot.bin the read returns no data and the `jmp CODE_SEG:0x0100000` lands in zeroed memory and triple-faults. Booting the full `os.bin` solves that.

## New test 08

Boots `bin/os.bin`, reads `info registers`, parses EIP. Expects EIP to land between 0x100000 and 0x101000 (proves the kernel was loaded and the bootloader jumped into it).

## Quirks worth noting

- The book's listing has `mov ebx, eax,` with a trailing comma at the start of `ata_lba_read`. NASM accepts it, but it is just sloppy. Kept as-is for verbatim fidelity. The trailing comma is on the same line as the comment so it does not change meaning.
- Reading 100 sectors when the kernel is currently only 23 bytes is wasteful but harmless. The remainder of os.bin is zero-padded so the read sees zeros. As the kernel grows toward 50 KiB we will need to bump the sector count.
- No status check on the ATA read. If the disk reports an error the routine ignores it and proceeds. We can add error handling later.
