# G10: task_list_remove missing next->prev symmetry

## Symptom

With three or more tasks in the schedule list, a terminating task in the
middle of the list silently corrupts the list: a later task is freed, then
its now-dangling `prev` pointer is dereferenced during a subsequent removal,
writing to freed memory and leaving the forward chain pointing at a freed
slot. The next PIT preempt then `task_switch`'es to that freed task,
loading garbage as CR3 and either triple-faulting or silently de-scheduling
some other task forever.

Concretely, when extending the Ch 150 demo from two tasks to five
(`Testing!`, `Abc!`, `CRASH`, `EXIT`, `BS`) the `Testing!` task never gets
scheduled after the first preempt: not because Testing!'s state is broken,
but because the list pointers around it become garbage after the transient
tasks die.

## Root cause

The book's `task_list_remove` only patches the forward link:

```c
if(task->prev){
    task->prev->next = task->next;
}
```

It never patches `task->next->prev`, so removal leaves stale `prev` pointers
behind. As long as the removed task was the tail (whose successor is NULL),
the bug is invisible. As soon as a middle task is removed and then a
later task is also removed, the second `list_remove` does:

```c
task->prev->next = task->next;   // writes via the STALE prev pointer
                                 // i.e. into freed memory of the earlier
                                 // removed task, not the real predecessor
```

so the real predecessor's `next` is never updated. The forward chain now
points at a freed task struct.

## Fix

Maintain both directions of the doubly linked list in `task_list_remove`:

```c
if(task->prev){
    task->prev->next = task->next;
}
if(task->next){
    task->next->prev = task->prev;
}
```

The `task->next` guard handles tail removal (where `next` is NULL).

## How it was found

While trying to add behavioural test scaffolding (a 5-task variant of the
Ch 150 demo where CRASH, EXIT, BS each exercise one post-Ch-145 code path),
the `Testing!` task stopped appearing on VGA entirely. A counter wired
into `isr80h_command1_print` confirmed Testing!'s `print` count stayed at
zero even after the kernel had been running long enough for ~90 round-robin
slices. Tracing back through the kill order:

1. CRASH null-derefs in its first slice. `list_remove(CRASH)` patches
   `A->next = E` but leaves `E->prev = CRASH` (now freed).
2. BS, then EXIT, call `samos_exit`. When `list_remove(EXIT)` runs,
   `EXIT->prev` is `A`, so `A->next = NULL` is set correctly here, but
   when an earlier middle task is removed before EXIT, the chain is
   already corrupt.
3. After all transients die, `A->next` points at one of the freed tasks.
   The next PIT does `task_switch(freed_task)`, loads garbage into CR3,
   and the scheduler is wedged on a ghost task.

Adding the `next->prev` symmetry restores the invariant and the demo
correctly cycles every task.

## Related

- [[G07-no-task-pre-iret]] - earlier scheduler-list invariant violation
- [[G09-pit-during-second-load]] - earlier scheduler-list timing bug
