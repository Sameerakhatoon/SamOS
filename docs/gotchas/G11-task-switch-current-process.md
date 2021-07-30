# G11: task_switch must update current_process too

## Symptom

The keyboard IRQ delivers chars to the wrong user task. Specifically,
sendkey'd characters never reach the `getkeyblock` loop in the KEY task
even when KEY is round-robin'd against the other tasks - the buffer they
land in belongs to whichever process was the LAST `process_load_switch`'d
at boot (or the most recent survivor after a death), regardless of which
task PIT actually scheduled at the moment IRQ1 fired.

## Root cause

`keyboard_push(c)` enqueues into `process_current()->keyboard.buffer`,
where `process_current()` returns the kernel's `current_process`. But
`current_process` is only ever written by `process_switch`, called from
`process_load_switch` and `process_switch_to_any`. **It is NOT touched
by `task_switch`** - which is what PIT preemption calls every time it
rotates the scheduler.

So `current_task` (the user code the CPU is executing) and
`current_process` (the user buffer the keyboard pushes to) drift apart
as soon as the first PIT IRQ fires. Every key the user types lands in
the SAME task's buffer until something calls `process_switch` -
normally only when a process dies.

In the multi-task demo with KEY + 9 other long-running tasks the bug
made caps-lock + sendkey untestable: every `sendkey a` enqueued to the
buffer of "whichever process was the last process_load_switch'd at
boot" - usually a task that never reads keys.

## Fix

Update `current_process` at the same time as `current_task`:

```c
int task_switch(struct task* task){
    current_task = task;
    paging_switch(task->page_directory);
    if(task && task->process){
        process_switch(task->process);
    }
    return 0;
}
```

After this `process_current() == task_current()->process` is an
invariant the keyboard subsystem (and anyone else reaching for "the
current user process") can rely on.

## How it was found

Adding test 50 (caps-lock behavioural via a forever-running KEY task
calling `samos_getkeyblock` + `samos_putchar`). The KEY task printed
zero K:* on serial no matter how many sendkey 'a' lines were piped to
the QEMU monitor. A probe over the serial mirror showed every key was
arriving (sendkey was firing IRQ1) but nothing was popping out of any
buffer. Tracing `keyboard_push` to `process_current()` revealed the
stale assignment: `current_process` had been set to the SH task during
the final `process_load_switch` at boot and never moved.

## Related

- [[G07-no-task-pre-iret]] - earlier scheduler-state invariant violation
- [[G10-task-list-remove-prev-symmetry]] - earlier scheduler-list bug
