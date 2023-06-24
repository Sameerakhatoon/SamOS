# Lecture 52 - restore IDT dispatch

**Source commit (PeachOS64BitCourse):** `e1790c0`
**SamOs commit:** L52 on `module1-64bit` branch
**Regression test:** `tests64/L52-idt-dispatch-restored.sh`

L38 stripped idt.c down to "just PIC ack" because the task /
process subsystem wasn't built. L45 brought the task subsystem
back. L52 un-comments the full dispatch path now that the
references resolve.

## What lights up

| Function | Restored body |
|---|---|
| `interrupt_handler` | kernel_page -> if callback registered: task_current_save_state + callback -> task_page -> PIC ack |
| `idt_handle_exception` | `panic("Panic Exception\n")` (will route to process_terminate + task_next once those are safer from IRQ context) |
| `idt_clock` | PIC ack -> print "test\n" (upstream debug breadcrumb) -> task_next |
| `idt_register_interrupt_callback(0x20, idt_clock)` | timer IRQ -> idt_clock |
| `isr80h_handler` | kernel_page -> task save -> dispatch -> task_page |

## Why we don't fire any of it yet

`interrupts are still disabled`. `kernel_main` has no `sti`
between idt_init and the wait loop. Without interrupts the
timer never fires; idt_clock is dead code; the full dispatch
loop is never exercised.

When the keyboard work (L53) needs interrupts on, it'll have
to make sure a current_task exists first - otherwise
`task_page()` on the first IRQ will deref NULL.

## How we verified

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
tss ready
```

Same as L50/L51. The L52 change is dormant until interrupts
turn on.

## Forward look

L53 restores keyboard. L54 - L60 fills in isr80h commands.
