# Lecture 33 - reimplement IO for 64-bit

**Source commit (PeachOS64BitCourse):** `1c1dba4`
**SamOs commit:** L33 on `module1-64bit` branch
**Regression test:** `tests64/L33-io-asm.sh`

## Why this chapter exists

The 32-bit `io.asm` used the cdecl calling convention - args
pushed on the stack, accessed via `[ebp+8]`, `[ebp+12]`:

```nasm
outb:
    push ebp
    mov  ebp, esp
    mov  eax, [ebp+12]   ; val
    mov  edx, [ebp+8]    ; port
    out  dx, al
    pop  ebp
    ret
```

In long mode under AMD64 SysV the convention changes: first six
integer args live in RDI, RSI, RDX, RCX, R8, R9. Stack-passed
args land at fixed positive offsets from RBP, not by index. So
we'd be reading garbage from `[ebp+8]` if we left the 32-bit
code in place.

L33 rewrites every IO helper for the new ABI. NEW dword
variants `insdw` / `outdw` for 32-bit port IO (some IO ranges
need them, e.g. PCI config space).

## API mapping

| Function | Port | Value |
|---|---|---|
| `insb (port)` | DI | -> AL (read) |
| `insw (port)` | DI | -> AX (read) |
| `insdw(port)` | DI | -> EAX (read) |
| `outb (port, val)` | DI | SI -> AL -> port |
| `outw (port, val)` | DI | SI -> AX -> port |
| `outdw(port, val)` | DI | SI -> EAX -> port |

`port` is 16 bits but lives in a 32/64-bit reg; we use `mov dx, di`
to get the low 16 bits.

`xor rax, rax` before the `in` instruction zeros the high bits so
the returned 8/16/32-bit value sits in a clean 64-bit RAX. The C
side casts down to `unsigned char` / `unsigned short` /
`unsigned int` per the prototype, so any high bits would be
discarded anyway, but explicit zeroing makes the asm easier to
audit.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/io/io.asm` | rewritten under AMD64 SysV. `[BITS 64]`. New `insdw` and `outdw` symbols. |
| `src/io/io.h` | adds `insdw` and `outdw` prototypes. |
| `Makefile` | `./build/io/io.asm.o` added to `FILES`; nasm `-f elf64` rule. |
| `build.sh` | `mkdir -p build/io`. |

## How we verified

VGA after L33:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
```

Same tokens as L32. No code calls the new io.asm symbols yet,
but they have to link cleanly because they're in FILES. A bad
asm directive or wrong selector would fail the link.

## Forward look

L34 grows the boot-time sector load count again so kernel.bin
has room for the rebuilt IDT (L35 - L37) and 64-bit IDT testing
(L38). After that, L39 adds user-mode GDT selectors and L40+
goes through the task / process / FAT16 / disk / keyboard /
isr80h reconstruction.
