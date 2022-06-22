# Ch 7 - Segmentation Memory Model

**Book pages:** 19-21 (Part 4)
**Code in this chapter:** none, prose

## The 20-bit problem

Real Mode wants to address 1 MB of physical memory (20-bit), but every register is only 16 bits wide. Maximum value a 16-bit register can hold is 65535 (0xFFFF), which is 64 KB.

The fix the 8086 designers picked: combine TWO 16-bit values per memory access. One is the "segment selector", the other is the "offset". The CPU produces the physical address with:

```
physical = (segment << 4) + offset
```

Shift the segment left by 4 bits (multiply by 16), then add the offset.

Worked example:
```
segment = 0x07C0
offset  = 0x0005
physical = (0x07C0 << 4) + 0x0005
         = 0x7C00 + 0x05
         = 0x7C05
```

That's why our bootloader assumes `ORG 0x7c00`: BIOS sets `CS=0x07C0, IP=0x0000`, which combine to physical 0x7C00.

## Segment registers in Real Mode

Four named segment registers, each used implicitly by different kinds of access:

| Register | Used by | Stands for |
|----------|---------|------------|
| `CS` | instruction fetches | Code Segment |
| `DS` | data loads/stores (default) | Data Segment |
| `ES` | string operations (`movs`, `lods`, `stos`) | Extra Segment |
| `SS` | stack (`push`, `pop`, `call`, `ret`) | Stack Segment |

When you write `mov word [0x05], ax`, the assembler implicitly uses `DS`. You can override with the `[ds:0x05]` or `[es:0x05]` syntax.

## How to set a segment register

You cannot load an immediate directly into a segment register. The pattern is two instructions:

```asm
mov ax, 0x07C0
mov ds, ax        ; DS = 0x07C0
```

## Disadvantages

- Memory becomes fragmented because segments can be different sizes scattered around.
- Address arithmetic gets fiddly: same physical byte can be reached by infinitely many (segment, offset) pairs.
- No protection: any segment can read/write any byte in the 1 MB space.

Protected Mode (later chapters) replaces this with a descriptor-based segmentation model that does enforce protection. For now we live with the warts.
