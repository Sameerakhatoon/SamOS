# Ch 24 - Understanding the Global Descriptor Table

**Book pages:** 58-61 (Part 5)
**Code in this chapter:** none, prose
**Reference code:** the GDT we already wrote in [Ch 22](Ch26-switching-to-protected-mode.md)

## A segment descriptor is 8 bytes

```
Bits:   0-15      16-31     32-39    40-47   48-51    52-55   56-63
Field:  Limit_L   Base_L    Base_M   Access  Limit_H  Flags   Base_H
Bytes:  2         2         1        1       half     half    1
```

Limit is 20 bits split across two fields. Base is 32 bits split across three. Access and flags are each one byte (flags shares a byte with the high nibble of Limit).

The book's `gdt_code` entry, byte by byte:

| Bytes (decimal) | Hex | Field |
|-----------------|-----|-------|
| 0-1 | `FF FF` | Limit bits 0-15 |
| 2-3 | `00 00` | Base bits 0-15 |
| 4 | `00` | Base bits 16-23 |
| 5 | `9A` | Access byte (1001 1010) |
| 6 | `CF` | Flags (1100) + Limit bits 16-19 (1111) |
| 7 | `00` | Base bits 24-31 |

So this descriptor describes a segment with base = 0, limit = 0xFFFFF (20 bits all set), and the granularity flag = 1 in flags. Effective size = (0xFFFFF + 1) * 4 KiB = 4 GiB.

## Access byte (bit 40 to bit 47)

| Bit | Name | For code segment | For data segment |
|-----|------|------------------|------------------|
| 40 | Accessed (A) | set by CPU on access | same |
| 41 | RW | readable | writable |
| 42 | DC | conforming | direction (expand-down) |
| 43 | Executable (E) | 1 for code | 0 for data |
| 44 | S (descriptor type) | 1 for code/data, 0 for system | same |
| 45-46 | DPL (privilege level) | 00 (ring 0) for kernel | same |
| 47 | Present (P) | 1 for valid | same |

`0x9A` = `1001 1010` = P=1, DPL=00, S=1, E=1, DC=0, RW=1, A=0 -> kernel code, readable, ring 0, not conforming.

`0x92` = `1001 0010` = P=1, DPL=00, S=1, E=0, DC=0, RW=1, A=0 -> kernel data, writable, ring 0, expand-up.

## Flags byte (bit 48 to bit 55, shared with limit high nibble)

| Bit | Name | Meaning |
|-----|------|---------|
| 52 | AVL | available for OS use; we leave it 0 |
| 53 | L | 1 = 64-bit code segment (we use 0 for 32-bit) |
| 54 | D/B | 1 = 32-bit operand size for code, 1 = 32-bit stack for SS |
| 55 | G | 1 = limit in 4 KiB pages; 0 = limit in bytes |

`1100` (bits 55..52 high nibble of the flags+limit byte) = G=1, D=1, L=0, AVL=0.

## How segment registers work in Protected Mode

In Real Mode `DS = 0x07C0` meant "data base address is 0x7C00". In Protected Mode `DS = 0x10` means "look up GDT entry at offset 0x10". The CPU walks the GDT, finds the descriptor, caches the base/limit/access internally, and uses those for every memory access through DS until DS is reloaded.

Net effect with the SamOs GDT (code base 0, data base 0, both 4 GiB limit): the CPU acts like there is no segmentation. Every linear address equals every effective address. This is called "flat" segmentation and is what every modern OS does. The real memory protection comes later from page tables.

## The GDTR

`LGDT [gdt_descriptor]` reads a 6-byte structure into the GDTR (Global Descriptor Table Register):

```
bytes 0-1: limit (size of GDT - 1, so max GDT = 64 KiB)
bytes 2-5: linear base address of the GDT
```

After `lgdt` the CPU knows where to find descriptors. The actual descriptor is not loaded into CS/DS/etc. until you write a selector to them (which we do with the far jump for CS and `mov ds, ax` etc. for the others).
