# Ch 117 - putchar syscall + keyboard_push null guard

The user program turns into a primitive terminal: every key the user presses gets echoed back through a putchar syscall.

## What landed

- `keyboard.c::keyboard_push` rejects c == 0 up front. Without this, a NULL pushed into the buffer would be indistinguishable from "empty slot" when keyboard_pop runs, causing getkey to loop on a real-but-invisible key. Pulled from book's "Addressing a Minor Issue" footer at the end of Ch 116.
- `isr80h.h` adds `SYSTEM_COMMAND3_PUTCHAR`.
- `io.h` exports `isr80h_command3_putchar`.
- `io.c` implements it:
  ```c
  void* isr80h_command3_putchar(struct interrupt_frame* frame){
      char c = (char)(int)task_get_stack_item(task_current(), 0);
      terminal_writechar(c, 15);
      return 0;
  }
  ```
- `isr80h.c` registers cmd 3.
- `kernel.h` exports `terminal_writechar` (needed by io.c).
- `blank.asm` becomes a typing echo loop:
  ```
  _loop:
      call getkey            ; blocks until next char
      push eax
      mov  eax, 3            ; PUTCHAR
      int  0x80
      add  esp, 4
      jmp  _loop
  ```

## Test-suite impact

- `tests/36-userland-prologue.sh` no longer asserts `EB FE` in blank.bin (the `jmp $` sentinel is gone; the loop is now `jmp _loop`). The check is now just "blank.bin exists and is non-trivial".
- `tests/38-syscall-roundtrip.sh` no longer pins EAX=0x32 (sum is gone). It still asserts CPL=3, CS=0x1B, EIP in `[0x400000, 0x400100]` after a keypress.
- `tests/39-syscall-print.sh` swaps the "I can talk with the kernel!" assertion for "the character 'A' appears on VGA" (QEMU `sendkey a` -> scancode 0x1E -> our table maps to 'A').
