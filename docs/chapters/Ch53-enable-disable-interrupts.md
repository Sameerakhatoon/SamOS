# Ch 49 - Implementing functionality to enable and disable interrupts in C

**Book pages:** 167-168 (Part 5)
**Code added:** `enable_interrupts` / `disable_interrupts` asm wrappers and prototypes, `sti` moved out of kernel.asm into `kernel_main`
**Test:** all 10 tests still pass; the keyboard tests in particular still see IRQ1 since interrupts are still enabled, just from C now

## What changed

Two one-line asm wrappers in `src/idt/idt.asm`:

```asm
enable_interrupts:
    sti
    ret

disable_interrupts:
    cli
    ret
```

Both are declared `global` and exposed via `src/idt/idt.h`:

```c
void enable_interrupts();
void disable_interrupts();
```

Now C can pause or resume hardware interrupts without needing inline assembly anywhere.

## The interrupt-window fix

The Ch 45 code had:

```asm
; kernel.asm
sti
call kernel_main
```

`kernel_main` then called `idt_init()` somewhere along the way. Between the `sti` and the actual `lidt` inside `idt_init`, the IDT was whatever was there before (zeros). If the PIT timer IRQ fired in that window, vector 0x20 would point at random memory and the CPU would triple-fault.

The window is short (a few microseconds), but the race was real and was specifically called out in Ch 45's "Quirks worth noting".

Ch 49 closes the window. The `sti` moves out of kernel.asm and lands in C right after `idt_init` finishes:

```c
void kernel_main(){
    terminal_initialize();
    print("Hello world!\ntest");
    kheap_init();
    idt_init();
    enable_interrupts();   // now safe: IDT is loaded
    ...
}
```

By the time `enable_interrupts` runs, every IDT entry points at either a real handler (vectors 0 and 0x21) or the `no_interrupt` stub. The earliest IRQ that can fire after `sti` has somewhere safe to land.

## What this unlocks

Now any kernel code can briefly disable interrupts when it needs atomicity. Typical use cases coming up:

- Critical sections that update shared data structures (process tables, scheduler queues).
- Updating the IDT itself.
- The `kmalloc` / `kfree` path (currently not needed because we are single-threaded, but useful when we have task switches).

The book uses the pattern `disable_interrupts(); ...; enable_interrupts();` instead of saving and restoring EFLAGS. Simpler but only correct as long as you do not nest these calls inside an existing critical section. We will accept the simpler version for now.
