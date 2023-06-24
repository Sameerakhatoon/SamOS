# Ch 45 - Implementing our PIC code

**Book pages:** 137-142 (Part 5)
**Code updated:** kernel.asm gains PIC remap and `sti`, idt.asm gains `int21h` and `no_interrupt` stubs, idt.c gains `int21h_handler`, `no_interrupt_handler`, and registers them, kernel.c no longer writes 0xFF to port 0x60
**Test:** `tests/11-keyboard-fires.sh`

## What we built

The kernel now responds to keyboard IRQs. Pressing a key produces "Keyboard pressed!" on the VGA screen.

The flow:

1. Boot, kernel.asm sets up segments, A20, then remaps the master PIC so IRQ0..IRQ7 land on vectors 0x20..0x27.
2. kernel.asm issues `sti` to enable interrupts.
3. kernel.asm calls `kernel_main`, which initializes the terminal, prints Hello world, then calls `idt_init`.
4. `idt_init` installs `no_interrupt` as the default for every vector, then overrides vector 0 with `idt_zero` (divide-by-zero) and vector 0x21 with `int21h` (keyboard).
5. When QEMU's keyboard fires IRQ1, the master PIC delivers vector 0x21 to the CPU.
6. The CPU pushes EFLAGS/CS/EIP, loads our IDT[0x21] descriptor, jumps to the `int21h` asm stub.
7. `int21h`: `cli` + `pushad` + `call int21h_handler` + `popad` + `sti` + `iret`.
8. `int21h_handler` (C) prints "Keyboard pressed!" and writes 0x20 to port 0x20 (EOI).

## PIC remap in kernel.asm

```asm
mov al, 00010001b           ; ICW1: init + ICW4 needed
out 0x20, al

mov al, 0x20                ; ICW2: master starts at vector 0x20
out 0x21, al

mov al, 00000001b           ; ICW4: 8086 mode
out 0x21, al

sti
```

Only the master PIC is remapped. Slave PIC and ICW3 are skipped because we are not using any IRQ in 8..15 yet.

## Why the asm wrappers around each handler

The CPU's interrupt mechanism pushes EFLAGS, CS, EIP automatically. It does NOT push general-purpose registers. If a handler clobbers EAX or EBX, the interrupted code returns to find its registers trashed.

So every interrupt entry point starts with `pushad` (push EAX, ECX, EDX, EBX, original ESP, EBP, ESI, EDI), runs the C handler, then `popad` to restore them.

`cli` and `sti` bracket the handler body. With a 32-bit interrupt gate (`type_attr = 0xEE`) the CPU has already cleared IF on entry, so `cli` is redundant. We keep it because the book does. `sti` re-enables IF before `iret`, but `iret` will restore IF from the saved EFLAGS anyway, so `sti` is also redundant. Belt and suspenders.

`iret` (not `ret`) pops EIP, CS, EFLAGS in that order. Plain `ret` would only pop EIP and leave CS and EFLAGS on the stack, breaking the next instruction's expectations.

## Default handler for unused vectors

```c
for(int i = 0; i < SAMOS_TOTAL_INTERRUPTS; i++){
    idt_set(i, no_interrupt);
}
idt_set(0, idt_zero);
idt_set(0x21, int21h);
```

Every vector gets `no_interrupt` first. Then we override specific ones. `no_interrupt_handler` just sends EOI so the PIC does not stall waiting for an acknowledgement we forgot to give.

## Why we send EOI to port 0x20

The PIC keeps an "in service" bit for each IRQ. As long as an IRQ is "in service", the PIC will not deliver any IRQ of equal or lower priority. The EOI command clears the in-service bit. Without it, the keyboard would deliver exactly one interrupt and then go silent.

Master PIC EOI: `outb(0x20, 0x20)`.
Slave PIC EOI: `outb(0xA0, 0x20)` (then also the master).

## Quirks worth noting

- **`sti` runs before `idt_init`.** Between the `sti` and the `lidt` inside `idt_init`, the IDT is whatever was there before (probably zeros). If the PIT timer IRQ fires in that window we triple-fault. The window is only a few microseconds long (terminal_initialize + print). At the PIT's default 18.2 Hz the window is small enough that the race almost never happens. The book accepts this. A more careful kernel would call `idt_init` before `sti`.

- **We do not drain port 0x60.** The keyboard controller stores the scancode there. Until we read it the IRQ1 line stays asserted, so as soon as we EOI, the PIC re-delivers IRQ1 immediately. In QEMU sending one key produces one "Keyboard pressed!" message because subsequent IRQs re-fire on top of each other and only one shows during our test window. The test sends only one key for that reason. A real keyboard driver (later chapter) will do `insb(0x60)` to drain.

- **`int21h_handler` calls `print` which uses VGA RAM at 0xB8000.** Print writes to terminal_row/col globals which are not protected with locks. With only one CPU and only one interrupt depth this is fine, but a re-entered handler (interrupts left enabled inside the C body) would corrupt the cursor state.

- **The `outb(0x60, 0xFF)` line from Ch 42 is removed.** Resetting the keyboard during kernel_main interferes with the PIC remap test.
