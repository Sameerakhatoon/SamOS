# Ch 10 - What Are Interrupts?

**Book page:** 27 (Part 4)
**Code in this chapter:** none, prose

## The three categories

| Category | Source | Example |
|----------|--------|---------|
| Hardware interrupt | Device sends a signal on the bus | Keystroke from PS/2 keyboard |
| Software interrupt | Program executes the `INT n` instruction | `INT 0x10` for BIOS video services |
| Exception | CPU raises an internal fault | Divide by zero (`#DE`), invalid opcode (`#UD`) |

## What "interrupt" means to the CPU

Before continuing the current instruction stream the CPU has to:

1. Push enough state to resume later (`CS`, `IP`, and `FLAGS` in real mode).
2. Look up which handler to run for this interrupt number.
3. Jump to the handler.
4. When the handler returns (`IRET`), pop the saved state and resume.

The lookup table is the IVT (next chapter).

## Why we care

Already used one in the bootloader: `INT 0x10` for BIOS teletype. Soon we will:

- Install our own ISR at vector 0x80 to demonstrate registering custom handlers.
- Install an ISR at vector 0 to catch divide-by-zero.
- Later, when we have protected mode and a kernel, replace the IVT with the IDT (a richer descriptor table).
