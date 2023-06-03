# Lecture 57 - call subsystem initializers

**Source commit (PeachOS64BitCourse):** `4eac25f`
**SamOs commit:** L57 on `module1-64bit` branch
**Regression test:** `tests64/L57-fs-disk-init.sh`

Three include lines and two function calls light up kernel_main:

| New call | What it does |
|---|---|
| `fs_init()` | registers the FAT16 driver with the generic VFS layer. After this, `fopen("0:/foo", "r")` knows to look at a FAT16-format filesystem when probing a disk. |
| `disk_search_and_init()` | walks the ATA channels (primary master via port 0x1F0), reads the boot sector, and binds the registered filesystem driver to disk 0. |

The keyboard include is uncommented too, but `keyboard_init`
itself isn't called yet - that comes in a later lecture (L68 in
upstream).

## Why it works

`fs_init` is a no-op-shaped function: it allocates filesystem
slots and registers FAT16. Nothing that could fail.

`disk_search_and_init` actually does I/O: ATA register
twiddling + the FAT16 boot-sector parse. SamOs has been
running on a properly-formatted FAT16 disk image since the
32-bit pass (see boot.asm BPB), so the boot-sector parse
recognises it.

If the parse had failed, `disk_search_and_init` would panic
("Failed to bind disk to filesystem" or similar). The
existing "tss load was fine" / "register isr80h" / "tss
ready" tokens still appearing is the proof we got through it.

## How we verified

VGA after L57:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
tss load was fine
register isr80h
tss ready
```

Same tokens as L55/L56. The fs/disk init runs silently between
`idt_init()` and the TSS work.

## Forward look

L58 builds a tiny user program. L59 - L60 load it. L61 remaps
the PIC so IRQ vectors don't collide with CPU exceptions. L66
fires the first ELF in long mode. L68 turns on the keyboard
IRQ.
