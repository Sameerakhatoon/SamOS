# Ch 43 - Programmable Interrupt Controller (PIC)

**Book pages:** 130-133 (Part 5)
**Code in this chapter:** none, prose

## What the PIC does

The PIC is the chip that sits between hardware devices and the CPU. Devices raise interrupt requests (IRQs); the PIC decides which one to forward to the CPU based on priority, asserts the CPU's INTR pin, and gives the CPU a vector number to look up in the IDT.

In x86 history:

- Original: Intel 8259A, 8 IRQ lines (IRQ0..IRQ7).
- IBM PC AT: two 8259As cascaded together via IRQ2, giving 15 usable IRQs (IRQ0..IRQ15 minus IRQ2 which is the cascade itself).
- Modern: APIC and IO-APIC, with per-CPU local APICs for multiprocessor systems. PIC still exists for legacy compatibility.

We use the 8259A pair (master + slave) for SamOs. APIC is a future-chapter problem.

## IRQ assignments

| IRQ | Device |
|-----|--------|
| 0   | System timer (PIT) |
| 1   | Keyboard controller |
| 2   | Cascade to slave PIC (do not use) |
| 3   | COM2 / COM4 |
| 4   | COM1 / COM3 |
| 5   | LPT2 / sound card |
| 6   | Floppy disk controller |
| 7   | LPT1 |
| 8   | Real-time clock |
| 9   | General use (often legacy) |
| 10  | General use |
| 11  | General use |
| 12  | PS/2 mouse |
| 13  | Math coprocessor |
| 14  | Primary ATA (IDE) channel |
| 15  | Secondary ATA channel |

The two we will care most about soon are IRQ 1 (keyboard) and IRQ 0 (timer, for preemptive multitasking).

## What a keypress does step by step

1. Press a key.
2. Keyboard controller stores the scancode in its data register at port 0x60 and pulses IRQ 1.
3. Master PIC sees IRQ 1, decides whether to forward (no higher-priority IRQ in progress, IRQ 1 not masked), asserts INTR to the CPU and writes the vector number on the bus.
4. CPU finishes the current instruction, pushes EFLAGS/CS/EIP, looks up the vector in the IDT, jumps to our handler.
5. Our handler reads the scancode from port 0x60 to clear it from the keyboard controller's buffer.
6. Our handler signals "End Of Interrupt" by writing 0x20 to PIC command port (0x20 for master).
7. Handler returns with `iret`.

## I/O ports the PIC uses

| Port | Purpose |
|------|---------|
| 0x20 | Master PIC command |
| 0x21 | Master PIC data (mask + ICW2/3/4) |
| 0xA0 | Slave PIC command |
| 0xA1 | Slave PIC data |

The next chapter walks through the ICW initialization sequence.
