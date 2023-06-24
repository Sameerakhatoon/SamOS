# Lecture 54 - register isr80h commands

**Source commit (PeachOS64BitCourse):** `2c4505a`
**SamOs commit:** L54 on `module1-64bit` branch
**Regression test:** `tests64/L54-isr80h-linked.sh`

L54 adds the isr80h directory to the build and calls
`isr80h_register_commands()` from `kernel_main`. After this
commit the kernel knows how to dispatch int 0x80 commands
0..4 (sum, print, getkey, putchar, malloc/free).

## What changes

| File | Change |
|---|---|
| `Makefile` | adds isr80h/isr80h.o, io.o, heap.o, misc.o, process.o to FILES + rules. |
| `build.sh` | mkdir build/isr80h. |
| `src/kernel.c` | uncomment task / process / isr80h includes; add isr80h_register_commands() call after gdt_set_tss. |
| `src/isr80h/io.c` | three intptr_t cast widenings (the char round trip + stack-item void* destructuring). |
| `src/isr80h/heap.c` | uintptr_t cast for the malloc-size out of a stack item. |
| `src/isr80h/misc.c` | intptr_t casts for the two integer operands in the sum demo. |

## Theory primer: the cast bug 32-bit got away with

The 32-bit isr80h code does things like:

```c
int v1 = (int)task_get_stack_item(task_current(), 0);
```

`task_get_stack_item` returns `void*`. On 32-bit, `void*` is 4
bytes and `int` is 4 bytes, so a `(int)` cast is a no-op
bit pattern reinterpretation. The cast diagnostic doesn't fire
because the sizes match.

On x86_64, `void*` is 8 bytes and `int` is 4 bytes. The
`(int)` cast truncates the high 32 bits. GCC's
`-Wint-to-pointer-cast` (and the reverse) treats this as
suspicious because data is being silently lost.

The fix is to route through `(intptr_t)` (signed pointer-sized
int) or `(uintptr_t)` (unsigned variant):

```c
int v1 = (int)(intptr_t)task_get_stack_item(task_current(), 0);
```

The pointer is first cast to a same-width int. Then truncating
to `int` is explicit and intentional - the user-side argument
fits in 32 bits anyway.

`isr80h_command4_malloc` is the analogous case for size_t. The
user passes a size_t-typed argument to malloc; we cast through
uintptr_t first.

## Why isr80h is dormant

No process has been launched and interrupts are still
disabled. `int 0x80` from kernel mode IS valid - you could
literally write `asm("int $0x80");` in kernel_main and it
would invoke isr80h_wrapper - but we have nothing in
kernel_main that does so. The command table sits populated
waiting for a future user task to call it.

## How we verified

Same tokens as L52/L53. The isr80h sources compile and link.
kernel_main reaches "tss ready" - meaning
`isr80h_register_commands()` returned without faulting (it
just fills a static array with function pointers).

## Forward look

L55+ probably loads and launches a user process. Once we have
a current_task we can enable interrupts (keyboard IRQ,
clock IRQ) and the dispatch path goes live.

The remaining outstanding work (per L50/L51/L52 notes):
- reload GDTR with wider limit so slot 7 (TSS) is reachable
- ltr the TSS
- ensure current_task is set before sti
