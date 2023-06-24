# Ch 98 - Communication with the kernel from user land (book Ch 101)

Book Ch 101: the conceptual primer for the syscall mechanism we'll
build over the next several chapters. Pure prose, no source changes.

## The model the book sets up

User-land code can't read kernel memory and can't issue I/O
instructions in ring 3. To ask the kernel to do something, it
fires `int 0x80` with an integer "command number" in EAX and any
arguments on the user stack:

```asm
; void print(const char* message)
print:
    push ebp
    mov  ebp, esp
    push dword [ebp+8]    ; the user's message pointer
    mov  eax, 1           ; command 1 = "print"
    int  0x80
    add  esp, 4
    pop  ebp
    ret
```

The CPU handles the ring-3 -> ring-0 transition for us via the TSS
we set up in Ch 90: SS:ESP swap to `esp0`/`ss0`, push user state,
load IDT[0x80]'s gate, dispatch into kernel code.

## What the kernel handler does

1. Looks at EAX -> dispatches to a command-specific implementation.
2. Walks the user stack (in user-land virtual addresses, but
   under the user's CR3 the kernel can still read it as long as
   we don't swap page tables yet).
3. For pointer arguments (like the message), converts the
   virtual address through the *user*'s page tables to a
   physical address, then accesses it from the kernel side -
   either by mapping the same physical address into the kernel's
   own address space, or by temporarily switching to the user
   page tables for the copy.
4. Does whatever the command needs (print, fopen, fread...).
5. Returns. `iretd` pops back to ring 3 where the user code
   continues right after its `int 0x80`.

## What lands in the next several chapters

- Ch 102 (book): `kernel_registers` (asm) + `kernel_page` (C).
  Mirror functions of `user_registers` + `task_page` from Ch 95
  but in the opposite direction: switch CR3 to the kernel's
  page directory and load DS/ES/FS/GS with the kernel data
  selector `0x10`. Necessary at the entry of every syscall
  handler so the kernel runs on its own segments and page tables
  regardless of which process invoked it.
- Ch 103+: the asm `isr80h_wrapper` that wraps the C handler with
  the register-save and stack-swap dance, plus the command
  dispatch table inside C.
- Ch 104+: per-command implementations (print, getkey, putchar,
  malloc, free, exit, ...).

## What the kernel doesn't need to do

- Validate that the user's command number is in range. We do
  that in the handler.
- Trust user pointers. Every pointer the user passes will be
  translated through the user PD to a physical address the
  kernel can access safely; if the translation fails we return
  an error code in EAX.

The chapter is mostly justifying the architecture, not adding
code. The implementation arc starts in Ch 102.
