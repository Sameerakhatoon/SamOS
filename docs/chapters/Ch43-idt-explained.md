# Ch 39 - Interrupt Descriptor Table (IDT)

**Book pages:** 112-115 (Part 5)
**Code in this chapter:** none, prose

## What the IDT is

In Real Mode the CPU used the IVT (Interrupt Vector Table) at physical 0x0000 with 256 entries of 4 bytes each. In Protected Mode the analogous structure is the IDT, but:

- Each entry is 8 bytes (not 4).
- The IDT can sit anywhere in memory; the CPU finds it via the `lidt` instruction which loads the IDTR register.
- Entries carry more metadata: present bit, descriptor privilege level, gate type.

## Gate descriptor layout (32-bit Protected Mode, 8 bytes)

```
bits 0..15   : offset bits 0..15 of the handler
bits 16..31  : code segment selector (points into the GDT)
bits 32..39  : reserved (zero)
bits 40..43  : gate type (0xE = 32-bit interrupt gate, 0xF = 32-bit trap gate)
bits 44      : storage segment (0 for interrupt/trap gates)
bits 45..46  : DPL (descriptor privilege level)
bit  47      : present (must be 1 for the entry to be used)
bits 48..63  : offset bits 16..31 of the handler
```

Translated to a C struct:

```c
struct idt_desc {
    uint16_t offset_1;     // bits 0..15 of the handler
    uint16_t selector;     // code segment selector
    uint8_t  zero;         // reserved
    uint8_t  type_attr;    // P, DPL, S, gate type
    uint16_t offset_2;     // bits 16..31 of the handler
} __attribute__((packed));
```

## Gate types

| Code | Type |
|------|------|
| 0x5  | Task Gate (offset unused, points to a TSS via selector) |
| 0x6  | 16-bit Interrupt Gate |
| 0x7  | 16-bit Trap Gate |
| 0xE  | 32-bit Interrupt Gate |
| 0xF  | 32-bit Trap Gate |

We use 0xE (32-bit interrupt gate) almost exclusively. The difference between interrupt and trap gates is whether the CPU automatically clears the interrupt flag (IF) on entry. Interrupt gates clear IF (so the handler runs with interrupts off by default). Trap gates leave IF alone (typical for debug exceptions).

## The IDTR

A 6-byte register the CPU consults when an interrupt fires:

```c
struct idtr_desc {
    uint16_t limit;   // size of the IDT minus 1
    uint32_t base;    // linear address of the IDT
} __attribute__((packed));
```

Loaded with `lidt [idtr_addr]`. After that, every `INT n`, hardware interrupt, or CPU exception causes the CPU to:

1. Push EFLAGS, CS, EIP (and an error code for some exceptions).
2. Look up entry n in the IDT.
3. Load CS with the selector, EIP with offset_1 | (offset_2 << 16).
4. Clear IF if it is an interrupt gate.
5. Start executing the handler.

## How this differs from the IVT

- IVT was fixed at physical 0; IDT can live anywhere.
- IVT entries were 4 bytes (just a far pointer); IDT entries are 8 bytes (far pointer plus permission bits).
- IVT entries had no concept of ring; IDT entries have DPL.

The next chapter wires up an actual IDT with one handler (divide-by-zero) and calls `lidt`.
