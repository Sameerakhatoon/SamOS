# Lecture 38 - bring the IDT up and test it with #DE

**Source commit (PeachOS64BitCourse):** `88207e5`
**SamOs commit:** L38 on `module1-64bit` branch
**Regression test:** `tests64/L38-idt-live.sh`

## Why this chapter exists

L35 - L37 ported the IDT code to 64-bit but kept it out of the
build because idt.c referenced the not-yet-rebuilt task /
process subsystems. L38 finally lights it up.

The trick: comment out every task/process reference in idt.c,
add idt.{o, asm.o} to FILES, and write a smoke test that
deliberately faults so we can observe the dispatch.

## The smoke test: idiv rax, 0

```nasm
div_test:
    mov rax, 0
    idiv rax     ; rax / rax with rax = 0 -> #DE
    ret
```

`idiv` divides rdx:rax by the operand. When the operand is 0,
the CPU raises #DE (vector 0). The IDT's vector-0 entry was
installed by `idt_set(0, idt_zero)` in `idt_init`, so the CPU
vectors to `idt_zero`:

```c
void idt_zero(){
    print("Divide by zero error\n");
    while(1) {}
}
```

The handler prints and spins forever. The `ret` in `div_test`
never executes; the `print("oi\n")` in `kernel_main` after
`div_test()` never runs.

If "oi" DID appear, one of three things went wrong:
- the IDT wasn't loaded (lidt failed or the descriptor was
  malformed)
- vector 0's gate pointed somewhere else
- idt_zero returned (would resume at the faulting instruction
  and re-fault immediately - in practice that's a TPR storm
  rather than a clean "oi")

## What got commented out in idt.c

| Function | What was there | What stays |
|---|---|---|
| `interrupt_handler` | `kernel_page() + task save + callback dispatch + task_page` | `outb(0x20, 0x20)` PIC ack only |
| `idt_handle_exception` | `process_terminate(task_current()->process); task_next();` | empty body |
| `idt_clock` | timer-IRQ -> task_next | empty body |
| `isr80h_handler` | `kernel_page + task_save + dispatch + task_page` | dispatch only |
| `idt_register_interrupt_callback(0x20, idt_clock)` | wired the timer | commented out |

Everything that needs a task / process module is parked behind
a comment. The IDT structure - install descriptors, lidt - works
fine without those. We get exception dispatch and PIC ack; we
just don't have anywhere to route higher-level events yet.

## kernel.asm globals

`div_test` is a 64-bit asm function (under the `[BITS 64]` band
in kernel.asm). It does the deliberate divide-by-zero. `ret`
after `idiv` is unreachable but kept so the asm symbol has a
well-formed end.

## How the change lands in our tree

| File | Change |
|---|---|
| `Makefile` | adds `idt.o` + `idt.asm.o` to FILES; build rules. |
| `build.sh` | `mkdir -p build/idt`. |
| `src/idt/idt.c` | comments out every task/process reference. `idt_zero` becomes print+halt. |
| `src/kernel.asm` | new `div_test` global - `mov rax, 0; idiv rax; ret`. |
| `src/kernel.c` | include `idt/idt.h`. After `multiheap_ready`, call `idt_init()`, print "hello", `div_test()`, print "oi" (the post-fault marker that must NOT appear). |

## How we verified

VGA after L38:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
hello
Divide by zero error
```

Pass conditions:

- "hello" appears: `idt_init()` returned cleanly. lidt accepted
  the IDTR; the 64-bit gate layout (16 bytes each) parsed.
- "Divide by zero error" appears: vector 0 dispatched
  correctly through the L=1 long-mode code selector
  (KERNEL_LONG_MODE_CODE_SELECTOR = 0x18) to idt_zero.
- "oi" is ABSENT: idt_zero's `while(1)` held; div_test never
  returned.

All three confirmed. The 64-bit IDT is live.

## Forward look

L39 adds the user-mode GDT selectors (DPL=3 code + data segs).
L40 - L42 rebuild the task subsystem in 64-bit. L43 adds
paging_desc_free (so dying processes don't leak page tables).
L44 - L46 close out the bring-up. L47 - L54 then walks through
disk / FAT16 / keyboard / isr80h / etc.
