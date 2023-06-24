# Ch 44 - Mapping the Programmable Interrupt Controller

**Book pages:** 134-136 (Part 5)
**Code in this chapter:** none, prose

## Why we have to remap

After reset the PIC delivers interrupts using vectors 0..7 (master) and 8..15 (slave). The CPU also reserves vectors 0..31 for its own exceptions (`#DE`, `#NMI`, `#GP`, `#PF`, etc.). The two ranges overlap.

If a hardware interrupt arrives while the PIC is still using its default mapping, the CPU sees a vector in [0, 31] and runs whichever exception handler is there. We do not want a keypress to fire `#DE`. So we remap the PIC to send vectors starting at 0x20 (master) and 0x28 (slave). That puts hardware IRQs at 0x20..0x2F, well away from CPU exceptions.

## ICWs: the four-byte handshake

To remap, we send four Initialization Command Words to each PIC.

| ICW | Where to send | What it does |
|-----|---------------|--------------|
| ICW1 | Command port (0x20 / 0xA0) | "Initialization mode begins. Expect ICW2, ICW3, ICW4." |
| ICW2 | Data port (0x21 / 0xA1) | The vector offset where this PIC's IRQs begin. |
| ICW3 | Data port (0x21 / 0xA1) | Cascade configuration. Master: which IRQ has the slave attached (bit 2 set = IRQ2). Slave: its identity (cascade number). |
| ICW4 | Data port (0x21 / 0xA1) | Environment flags. Bit 0 = "8086 mode". |

After ICW1..ICW4 the PIC is ready. Subsequent writes to the data port modify the interrupt mask (which IRQs are blocked).

## The resulting vector map

| IRQ | Vector | Device |
|-----|--------|--------|
| 0   | 0x20   | Timer |
| 1   | 0x21   | Keyboard |
| 2   | 0x22   | Cascade (unused) |
| 3   | 0x23   | COM2 |
| 4   | 0x24   | COM1 |
| 5   | 0x25   | LPT2 / sound |
| 6   | 0x26   | Floppy |
| 7   | 0x27   | LPT1 |
| 8   | 0x28   | RTC |
| ... | ...    | ... |
| 14  | 0x2E   | Primary ATA |
| 15  | 0x2F   | Secondary ATA |

After remap, `idt_set(0x21, keyboard_handler)` and `idt_set(0x20, timer_handler)` reach the right device handlers.

## End-of-interrupt acknowledgement

When the handler finishes, it must tell the PIC "I am done with this IRQ" by writing 0x20 (the EOI code) to the PIC command port (0x20 for master, 0xA0 for slave). Without EOI the PIC remembers an interrupt is in service and refuses to deliver any IRQ of equal or lower priority. Forget the EOI once and the keyboard goes silent forever.

For interrupts from the slave PIC, both PICs need the EOI: write 0x20 to 0xA0 first (slave) then 0x20 to 0x20 (master).

## What the next chapter implements

The actual `outb` sequence in `kernel.asm` for the master PIC remap, plus a stub keyboard handler (`int21h_handler`) that prints "Keyboard pressed!" and sends EOI. Slave PIC remap is omitted for now since we are not using any IRQ 8..15 yet.
