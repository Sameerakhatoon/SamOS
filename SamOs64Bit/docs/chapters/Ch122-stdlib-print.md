# Ch 128 - stdlib print() + blank.c uses it

The user-space C program gets its first standard-library function: `print`. Wraps the existing SYSTEM_COMMAND1_PRINT syscall. The new `main` calls `print(...)` then enters an infinite loop, preventing the Ch 127 ret-into-uninit-stack triple fault.

## What landed

- `programs/stdlib/src/samos.asm` - low-level `print` routine:
  ```asm
  print:
      push ebp
      mov  ebp, esp
      push dword [ebp+8]    ; const char* msg from caller's stack
      mov  eax, 1           ; SYSTEM_COMMAND1_PRINT
      int  0x80
      add  esp, 4
      pop  ebp
      ret
  ```
- `programs/stdlib/src/samos.h` - exports `void print(const char* msg);`
- `programs/stdlib/Makefile` - now assembles `samos.o` and links it into `stdlib.elf` alongside `start.o`.
- `programs/blank/blank.c`:
  ```c
  #include "samos.h"
  int main(int argc, char** argv){
      print("Hello how are you!\n");
      while(1){}
      return 0;
  }
  ```

The `while(1)` is what restores the suite to green: it ensures `_start`'s trailing `ret` is never reached, so the user task no longer triple-faults.

## File-naming note

The book calls the file `peachos.asm`/`peachos.h`. Per project convention (no mentions of any other project), we use `samos.asm`/`samos.h`. The functions inside have the same names the book uses.

## Test impact

Ch 127's red tests come back green at this commit. No new tests needed yet - this is a behaviour restoration, not a new feature surface.
