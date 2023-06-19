# Lecture 67 - IRQ enable / disable C API

**Source commit (PeachOS64BitCourse):** `3bad158`
**SamOs commit:** L67 on `module1-64bit` branch
**Regression test:** `tests64/L67-irq-c-api.sh`

The L61 PIC remap set the initial IRQ mask to:
- master 0xFB = 1111 1011 = all masked except IRQ2 (cascade)
- slave 0xFF = all masked

That's a "nothing fires" baseline. L67 adds the C helpers that
let later code (keyboard driver, ATA driver if we get that far,
etc) flip individual IRQ bits without re-doing the whole ICW1..4
init sequence.

## Theory primer: the PIC mask register

The mask register lives at the DATA port of each PIC:
- master: 0x21
- slave: 0xA1

Bit N of that byte = "IRQ N masked". Set bit = masked (no
delivery). Clear bit = unmasked (PIC delivers the IRQ to the
CPU).

`insb(port)` reads the current mask. We OR or AND to flip one
bit. `outb(port, ...)` writes it back. No locking is needed
because port IO from the kernel is already in a single-threaded
context (we don't preempt the kernel itself; only user tasks
get preempted).

## Master vs slave routing

```c
if (irq >= PIC_SLAVE_STARTING_IRQ) {
    port = IRQ_SLAVE_PORT;
    relative_irq = irq - PIC_SLAVE_STARTING_IRQ;
}
```

IRQ 8..15 are on the slave PIC, addressed as bits 0..7 of the
slave mask. The relative_irq calculation handles that.

## What's missing

Calling `IRQ_enable(IRQ_KEYBOARD)` from kernel_main right now
would let the keyboard IRQ through, but `interrupt_handler` ->
`task_page()` would deref NULL because no task is current at
the moment we'd want to receive keystrokes. L68 fixes this by
calling `IRQ_enable` only after a process is loaded.

## How we verified

VGA after L67 (and L68 will use these helpers):

```
...
tss ready
Loading program...
user enter
```

Same tokens as L66. `build/idt/irq.o` is in the link.

## Forward look

L68 - keyboard IRQ live: `IRQ_enable(IRQ_KEYBOARD)` + sti
somewhere safe.
