# Ch - The Hello World Bootloader

**Book pages:** 13-15 (Part 4 - Real Mode Development, Section 3)
**Code added:** real `src/boot.asm`, real `build.sh`
**Test:** `tests/02-bootloader-prints-hello.sh`

## What we built

A 512-byte boot sector that:

1. Sets origin to `0x7c00` (where BIOS loads it).
2. Loads the address of a NUL-terminated string into `SI`.
3. Calls a `print` routine that loops `lodsb` → `cmp al, 0` → BIOS teletype (`INT 0x10, AH=0x0E`).
4. Spins forever (`jmp $`) after printing.
5. Pads to 510 bytes and writes `0xAA55` as the boot signature.

## Build

```bash
nasm -f bin src/boot.asm -o bin/boot.bin
cp bin/boot.bin bin/os.bin
```

Output is exactly 512 bytes ending `55 AA`.

## Run

```bash
qemu-system-x86_64 -hda bin/os.bin
```

SeaBIOS posts, prints its banner, then jumps to our boot sector at `0x7c00`. Our code runs and writes "Hello, World!" to the screen using INT 0x10's teletype service. The cursor advances after the SeaBIOS banner, so the message appears just below it.

## Test approach

The bootloader doesn't use the serial port (BIOS teletype writes to VGA text RAM at physical `0xB8000`). The test uses QEMU's monitor command `pmemsave 0xB8000 4096 <file>` to dump the VGA buffer after a 2-second delay, then scans the dump for the bytes spelling "Hello, World!". VGA text mode stores each cell as a 2-byte (char, attribute) pair - strip the odd bytes before grepping.

## Quirks worth noting

- The book's listing has `mov bx, 0` at the top of `print`; `BX` is never read. Kept verbatim.
- The `.loop` and `.done` are local labels inside `print` (NASM scopes them to the preceding non-local label).
- Origin matters: without `ORG 0x7c00` the assembled offsets for `message` would be wrong relative to where BIOS places the code.
