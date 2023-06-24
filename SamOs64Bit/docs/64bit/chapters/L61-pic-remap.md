# Lecture 61 - PIC remap

**Source commit (PeachOS64BitCourse):** `ac2cfbb`
**SamOs commit:** L61 on `module1-64bit` branch
**Regression test:** `tests64/L61-pic-remap.sh`

## Why this chapter exists

L59-L60 ran into a hard collision: IRQ0 (timer) and CPU
exception #DF (double fault) both vector at 0x08. After ring-3
entry with IF=1, the first timer tick triggered our IDT[8]
which is registered as `idt_handle_exception` -> `panic`.

L61 reprograms the 8259 master/slave PICs so their IRQ
vectors don't overlap with CPU exceptions.

## Theory primer: the PIC init sequence

The 8259 takes four "Initialisation Control Words" sent in
order:

| ICW | Port | Master value | Slave value | Meaning |
|---|---|---|---|---|
| ICW1 | command (0x20, 0xA0) | 0x11 | 0x11 | start init, cascade, ICW4 will follow |
| ICW2 | data (0x21, 0xA1) | 0x20 | 0x28 | vector base |
| ICW3 | data | 0x04 | 0x02 | master: slave on IRQ2 bit (bit 2 = 0x04); slave: cascade identity (= 2) |
| ICW4 | data | 0x01 | 0x01 | 8086/88 mode |

After ICW1 the PIC awaits ICW2..ICW4 on its DATA port
(0x21 / 0xA1) in order, then resumes normal operation.

The OCW1 mask (sent to the DATA port AFTER initialisation) is
a byte where bit N = 1 masks IRQN. Master 0xFB = 11111011
masks all except IRQ2 (the slave cascade). Slave 0xFF masks
everything on the slave.

Finally an EOI (0x20) to each PIC clears any in-flight
interrupt the chip might have been holding.

## Why we mask IRQ0 at this point

Upstream L61 leaves IRQ0 (bit 0) masked too. SamOs follows.

The reason: we don't yet have a proper task scheduler that
can safely preempt a user task on every timer tick.
`task_next` works because there's only one task; preempting
just resumes the same task. That's fine until we add a
keyboard driver that wants to deliver scancodes to a SPECIFIC
task and expects task_current to be deterministic.

A later lecture will unmask IRQ0 (or move idt_clock to be
lighter-weight) once the scheduler can handle preemption
cleanly.

## What changes for SamOs

| File | Change |
|---|---|
| `src/kernel.asm` | 33 lines of PIC init in `long_mode_entry`, before the `jmp kernel_main`. ICW1..4 to both PICs + IRQ masks (0xFB master / 0xFF slave) + EOI ack. |

## How we verified

VGA after L61:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
tss load was fine
register isr80h
tss ready
Loading program...
user enter
```

**No "Panic Exception"** - the timer is masked, so IRQ0 never
fires, so the spurious-#DF problem disappears. The user code
(jmp $) runs silently in CPL=3 from "user enter" onward.

The test asserts the absence of "Panic Exception" as well as
the presence of "user enter", so a regression that re-enabled
the timer without fixing the dispatch path would fail it.

## Forward look

L62 rebuilds stdlib so user programs can be C. L63 re-enables
ELF loading. L65 refactors the ELF32 loader to ELF64. L66
runs the first ELF user program. L68 unmasks the keyboard
IRQ (IRQ1).
