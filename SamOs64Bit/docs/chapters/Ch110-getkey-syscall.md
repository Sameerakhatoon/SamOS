# Ch 116 - getkey syscall (cmd 2) + blank.asm reads keyboard

The user program now blocks until a key is pressed before doing anything else. The mechanism: a new syscall `SYSTEM_COMMAND2_GETKEY` that returns the front character of the active process's keyboard buffer (or 0 if empty).

## What landed

- `isr80h.h` adds `SYSTEM_COMMAND2_GETKEY = 2` to the enum.
- `io.h` exports `isr80h_command2_getkey`.
- `io.c` implements it:
  ```c
  void* isr80h_command2_getkey(struct interrupt_frame* frame){
      char c = keyboard_pop();
      return (void*)((int)c);
  }
  ```
- `isr80h.c` registers cmd 2 in `isr80h_register_commands`.
- `programs/blank/blank.asm` rewrites to:
  ```
  call getkey            ; blocks until a key arrives
  push message           ; "I can talk with the kernel!"
  mov  eax, 1            ; PRINT
  int  0x80
  ; ... sum, jmp $
  getkey:
      mov eax, 2         ; GETKEY
      int 0x80
      cmp eax, 0
      je  getkey         ; tight poll loop
      ret
  ```

## Book bug shipped here

`keyboard_pop` in Ch 110 was preserved with its inverted `if (task_current()) return 0;`. With this Ch 116 wiring active, the user always blocks in `getkey` forever - `keyboard_pop` returns 0 unconditionally whenever a task is running, which is exactly when getkey is called. Tests 38 and 39 fail because `blank.bin` never reaches print/sum.

The fix lands in `g06` immediately after this commit: flip the condition to `if (!task_current()) return 0;` and update tests 38/39 to send a key before sampling state.
