# Ch 42 - Implementing IN and OUT in the Kernel

**Book pages:** 126-129 (Part 5)
**Code added:** `src/io/io.asm`, `src/io/io.h`, Makefile + build.sh expanded, kernel_main writes 0xFF to port 0x60
**Test:** all 7 tests still pass

## What we built

Four assembly wrappers callable from C:

```
unsigned char  insb(unsigned short port);
unsigned short insw(unsigned short port);
void           outb(unsigned short port, unsigned char  val);
void           outw(unsigned short port, unsigned short val);
```

Each follows the same skeleton:

```asm
insb:
    push ebp
    mov ebp, esp
    xor eax, eax        ; clear EAX so the return is just AL
    mov edx, [ebp+8]    ; first arg = port number
    in al, dx
    pop ebp
    ret
```

`outb` is the same shape with one extra `mov eax, [ebp+12]` to pull the value off the stack before issuing `out`.

## kernel_main writes 0xFF to port 0x60

Port 0x60 is the PS/2 keyboard data port. Writing 0xFF to it sends a "reset" command to the keyboard controller. The book wants a quick visible sign that our `outb` wrapper actually executes (the keyboard self-test starts). In QEMU we cannot observe that signal directly without polling, so the only real check is "the kernel ran outb without faulting".

```c
void kernel_main(){
    terminal_initialize();
    print("Hello world!\ntest");
    idt_init();
    outb(0x60, 0xFF);
}
```

## Calling convention

System V i386 ABI:

- Arguments pushed right-to-left.
- First arg lands at `[ebp+8]` (after the saved EBP at `[ebp+4]` and the return address at `[ebp+0]`).
- Second arg at `[ebp+12]`.
- Return value in EAX (truncated to AL or AX as needed).

For `outb(port, val)` the stack looks like:

```
[ebp+12]  val   (unsigned char, padded to 4 bytes on stack)
[ebp+8]   port  (unsigned short, padded to 4 bytes)
[ebp+4]   return address
[ebp+0]   saved ebp
```

The functions zero EAX before `in al, dx` so the upper bytes of the returned 32-bit value are zero. Without `xor eax, eax` the caller would see whatever junk was in EAX, masked to AL.

## Why this matters going forward

Almost every device interaction routes through these four wrappers:

- Keyboard read: `insb(0x60)`.
- Keyboard write: `outb(0x60, cmd)`.
- PIC remap and EOI: `outb(0x20, ...)`, `outb(0xA0, ...)`.
- ATA register I/O: `insb`/`outb` to ports 0x1F0-0x1F7.
- PIT (timer): `outb(0x43, ...)`, `outb(0x40, ...)`.

The next chapter (PIC) starts using them right away.

## Test impact

Behavior visible to tests is unchanged. The boot still reaches the kernel, terminal still shows Hello world, EIP still ends up inside the kernel image. The `outb(0x60, 0xFF)` does its work silently. All 7 tests still pass.
