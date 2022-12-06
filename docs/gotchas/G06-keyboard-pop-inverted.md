# G06 - keyboard_pop's flipped early-return makes getkey block forever

## Where it lives

`src/keyboard/keyboard.c::keyboard_pop`.

## What the book ships

```c
char keyboard_pop(){
    if (task_current()) {      // <- inverted
        return 0;
    }
    struct process* process = task_current()->process;
    int real_index = process->keyboard.head % sizeof(process->keyboard.buffer);
    char c = process->keyboard.buffer[real_index];
    ...
}
```

The early-return is logically a "no current task -> nothing to pop" bail-out, but the condition is the wrong sign. When a task IS running (the only time anyone calls this), the function returns 0; the rest of the body that actually pops the buffer is unreachable.

## What goes wrong

After Ch 116, the user program does:

```
getkey:
    mov eax, 2     ; SYSTEM_COMMAND2_GETKEY
    int 0x80
    cmp eax, 0
    je  getkey     ; loop until non-zero
    ret
```

`isr80h_command2_getkey` returns `keyboard_pop()`. With the inverted guard, that's always 0, so the user spins in getkey forever. The print + sum syscalls that follow never execute, and tests 38 and 39 hang past their 30s timeout.

## How to surface it

Run any test that depends on blank.bin reaching code past `call getkey`. The cleanest reproducer is `tests/39-syscall-print.sh` after Ch 116 - the VGA buffer never contains "I can talk with the kernel!" regardless of how many keys are sent.

## The fix

Flip the sign:

```c
if (!task_current()) {
    return 0;
}
```

Now the bail-out triggers only when there is no active task; with one, the body runs and the buffer is drained.

## Surfaced

Chapter 116 (Reading keys from user land).

## Fix commit

`g06: fix - flip keyboard_pop early-return condition`.
