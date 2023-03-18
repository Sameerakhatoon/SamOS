# Lecture 9 - restore simple terminal functionality

**Source commit (PeachOS64BitCourse):** `829161f`
**SamOs commit:** L9 on `module1-64bit` branch
**Regression test:** `tests64/L09-hello-terminal.sh`

## Why this chapter exists

Lecture 8 proved C runs in long mode but had no way to print
anything - we used `info registers` over QEMU's monitor as the
only observable. Lecture 9 brings the VGA terminal helpers back
so `kernel_main` can paint progress to screen with a single
`print()` call.

This is the same code SamOs already had in 32-bit, just dropped
back into the 64-bit build. Everything compiles cleanly with
`x86_64-elf-gcc`; the 32-bit-to-64-bit gotchas come later (when
pointer-arithmetic is non-trivial, when we start dealing with
size_t at boundaries, etc).

## Theory primer

### 1. The VGA text-mode buffer

In real-mode / protected-mode legacy compatibility, the BIOS
maps a 4 KiB region at physical 0xB8000 to the VGA text frame
buffer. Each on-screen cell is **2 bytes**:

```
+--------+--------+
| char   | attr   |
+--------+--------+
  byte 0  byte 1
```

The attribute byte encodes foreground colour (low nibble, 0-15)
and background colour (high nibble, 0-15). 0x0F = bright white
on black.

The buffer is laid out **row-major**: cell at (col, row) lives at
offset `(row * 80 + col) * 2`. 80 columns × 25 rows = 4000 bytes
in use; the rest of the page is padding.

This works in long mode because the first 4 MiB are identity-
mapped (PD_Table entry 0). The address 0xB8000 falls in that
range, so a 64-bit write hits VGA RAM the same way a 32-bit one
would.

### 2. Why we use `uint16_t*`, not two separate `uint8_t` writes

The terminal_make_char/putchar helpers build a 16-bit value (`(colour << 8) | c`)
and write it as one store. Single-store cells make sure the char
and attr land atomically - no risk of an IRQ landing between
them and seeing a half-written cell. In our kernel today there
are no IRQs to interrupt the write, but the habit is cheap and
makes the 32-bit and 64-bit code identical.

### 3. Why this code didn't need a 32-bit→64-bit rewrite

- `video_mem` is a `uint16_t*` - pointer sizes change (4 → 8
  bytes), but the API doesn't expose the pointer width.
- `terminal_row` and `terminal_col` are `uint16_t` - fixed-width,
  no port concerns.
- `strlen()` returns `size_t` - was 4 bytes on 32-bit, 8 bytes on
  64-bit. The loop counter was promoted from `int` to `size_t`
  for cleanliness; gcc would have warned about the implicit
  narrowing under `-Wall`.

That's it. The whole driver compiles unchanged under
`x86_64-elf-gcc -ffreestanding`.

### 4. `print()` is the boundary

`print()` is the only public entry. Internal helpers
(`terminal_*`) could change signature later (e.g., when font
rendering replaces text-mode in Lecture 92+), but `print()`
stays so the rest of the kernel doesn't notice the swap.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/kernel.c` | replaces the L8 stub. Adds VGA terminal helpers + `print()`. `kernel_main` now does `terminal_initialize(); print("Hello 64-bit!\n"); while(1) {}` |
| `Makefile` | `FILES += ./build/string/string.o`; new rule for `build/string/string.o` from `src/string/string.c` |
| `build.sh` | `mkdir -p build/string` so the build dir exists |

`src/string/string.c` and `src/string/string.h` already live in
the SamOs tree from the 32-bit work (Ch 58, 64, 69 - `strlen`,
`strnlen`, `strcpy`, `strncmp`, etc.). They compile cleanly under
`x86_64-elf-gcc` with no change.

## How we verified

`tests64/L09-hello-terminal.sh`:

1. Builds the kernel.
2. Boots under QEMU TCG with `-vga std`, waits ~2 s.
3. Sends `pmemsave 0xb8000 4096 ...` to dump the VGA frame
   buffer.
4. De-interleaves the char/attr pairs, replaces NUL with space,
   greps for `Hello 64-bit!`.

Observed:

```
$ od ... vga.dump | awk ... | xxd -r -p | tr '\0' ' '
Hello 64-bit!
```

`Hello 64-bit!` on the first row of VGA = C in long mode reached
`print()` and wrote 16 cells. End to end.

## What is still missing

- No newline scrolling logic - Lecture 9 keeps the same
  "increment row, never scroll" behaviour the 32-bit kernel
  started with (we added `terminal_scroll` later as a SamOs
  extra). Lecture 96+ overhauls this when the pixel-mode terminal
  arrives.
- No COM1 serial mirror yet - Lecture 9 is VGA-only. The serial
  helpers we wrote in SamOs (`serial_init`, mirror inside
  `terminal_writechar`) are NOT carried into the 64-bit branch
  yet. They come back when we set up COM1 in 64-bit (probably
  alongside the IDT work).
- No interrupts, no keyboard, no heap - same scope as L8.

## Next lecture

Lecture 10 - restore the kernel heap. Bring back `kheap_init`,
`kmalloc`, `kfree`, and the heap descriptor structures. After
this, `kernel_main` can allocate.
