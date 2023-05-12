# Lectures 46 + 47 - FAT16 + disk into the 64-bit build

**Source commits (PeachOS64BitCourse):** `5f9ceeb` (L46), `494e092` (L47)
**SamOs commit:** L46-L47 on `module1-64bit` branch
**Regression test:** `tests64/L46-L47-fs-disk-linked.sh`

## Why this chapter exists

Upstream's L46 adds fs/fat/fat16.o, fs/file.o, fs/pparser.o to
FILES. The link FAILS at L46 alone because fat16.c calls
`diskstreamer_*` and file.c calls `disk_get`, both of which
live in disk/ - which is added in L47.

SamOs lands both in one commit to keep every commit on
`module1-64bit` buildable.

## What changed

| File | Change |
|---|---|
| `Makefile` | adds fs/file.o, fs/pparser.o, fs/fat/fat16.o, disk/disk.o, disk/streamer.o to FILES; rules. |
| `build.sh` | mkdir build/fs build/fs/fat build/disk. |
| `src/kernel.h` | ISERR macro casts through int64_t before truncating to int. |

No source-file edits in fs/ or disk/. The 32-bit sources happen
to be pointer-width-clean (uintptr_t / uint32_t handled
correctly; no `(unsigned int)ptr` style truncations) so they
recompile under x86_64-elf-gcc as-is.

## Theory primer: why the ISERR widen

```c
// before
#define ISERR(value) ((int)value < 0)

// after
#define ISERR(value) ((int)((int64_t)(value)) < 0)
```

When `value` is a pointer (or a small negative int already
sign-extended to int64_t via ERROR), the bare `(int)value`
cast TRUNCATES the high 32 bits. If the high 32 bits hold
sign-extension of `-EIO` etc, truncating leaves us with `(int)0
< 0` -> false, and the caller misses a real error.

Routing through int64_t first preserves the sign across the
truncation. fat16 / file are heavy ISERR users; this is the
fix that makes the build correct under 64-bit pointers, not
just buildable.

## How we verified

VGA after L46+L47:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
hello
Divide by zero error
```

Same tokens as L38 - L45. Nothing actually opens a file or
reads a sector at L46+L47. The test confirms the link works
and the IDT path is still alive.

## Forward look

L48 (or thereabouts) brings the process subsystem the rest of
the way back. L49 - L51 builds the TSS and a C-side GDT
builder. L52 - L53 restores the keyboard + IRQ wiring. L54+
fills in the isr80h command table.
