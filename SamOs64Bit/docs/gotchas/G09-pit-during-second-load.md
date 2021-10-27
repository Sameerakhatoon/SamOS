# G09 - PIT IRQ during the second `process_load_switch` hijacks the kernel before task 2 is created

**Surfaced during:** Ch 150 (PIT-driven multitasking) once `kernel_main` started loading TWO `blank.elf` tasks. As soon as the first `process_load_switch` set `current_process`, the next PIT IRQ ran `idt_clock -> task_next`, which iretd into task 1 and never returned - so task 2 was never created.
**Fix:** `disable_interrupts()` immediately before the dual-load section in `kernel_main`. `task_return` re-enables IF in the synthesized user EFLAGS image, so userland still gets PIT preemption - the kernel just stays single-threaded across the loads.
**Regression test:** `tests/41-multitasking.sh` requires ≥5 `Testing!` AND ≥5 `Abc!` tokens, which only holds if BOTH tasks were created and scheduled.

## Where it lives

`src/kernel.c::kernel_main`, around the dual blank.elf load Ch 150 introduces.

## What goes wrong

Ch 150 ends with:

```c
process_load_switch("0:/blank.elf", &process);   // creates task 1
process_inject_arguments(process, ...);
process_load_switch("0:/blank.elf", &process);   // SHOULD create task 2
process_inject_arguments(process, ...);
task_run_first_ever_task();
```

After the FIRST `process_load_switch`, `current_task` and `current_process` point at task 1. Interrupts are still enabled (from `enable_interrupts()` earlier in kernel_main). The very next PIT IRQ - and PIT fires every ~55 ms, so almost certainly before the second `process_load_switch` finishes - hits `idt_clock` → `task_next`.

`task_next` calls `task_get_next` which returns `task_head` when there's only one task (wrap-around). Then `task_switch(task_head); task_return(&task_head->registers)` → iretd to task 1's user mode. The kernel never returns to finish the second `process_load_switch`, so task 2 is never created.

Visible symptom: VGA fills with only "Testing!" forever; "Abc!" never appears no matter how long you sample.

## How to surface it

Sample VGA at a few different times. If task 2 isn't running you'll see "Testing!" count saturate (≈151 cells full of "Testing!") and "Abc!" count stay at 0:

```
t=2s   Testing=151  Abc=0
t=15s  Testing=151  Abc=0
t=20s  Testing=151  Abc=0
```

With the fix:

```
t=2s   Testing=109  Abc=84
t=15s  Testing=70   Abc=163
```

The ratio varies because we're sampling a preemptive scheduler.

## The fix

Bracket the dual-load in a critical section. `disable_interrupts()` before the first `process_load_switch`; `task_return` inside `task_run_first_ever_task` then re-enables them by pushing an EFLAGS image with IF=1 onto the synthetic iret frame, so the user task starts with interrupts on.

```c
disable_interrupts();
process_load_switch("0:/blank.elf", &process);
inject("Testing!");
process_load_switch("0:/blank.elf", &process);
inject("Abc!");
task_run_first_ever_task();   // task_return iretd's with IF=1
```

## Surfaced

Chapter 150 (timer-driven multitasking). The single-task path (kernel loading only `shell.elf` and friends) never tripped this because `task_get_next` wrapping back to the same task is the desired behaviour, not a hijack.

## Fix commit

`g09: disable_interrupts across the dual blank.elf load in kernel_main; task_return re-enables in the user EFLAGS image`.
