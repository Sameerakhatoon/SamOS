# Ch 129 - stdlib getkey() + blank.c reads keys

stdlib gains a second routine, `getkey()`, wrapping SYSTEM_COMMAND2_GETKEY. `blank.c` becomes a key-watcher loop that prints "key was pressed" on each keypress.

## What landed

- `programs/stdlib/src/samos.asm` appends:
  ```asm
  global getkey:function
  getkey:
      push ebp
      mov  ebp, esp
      mov  eax, 2            ; SYSTEM_COMMAND2_GETKEY
      int  0x80
      pop  ebp
      ret
  ```
- `programs/stdlib/src/samos.h` exports `int getkey();`.
- `programs/blank/blank.c`:
  ```c
  #include "samos.h"
  int main(int argc, char** argv){
      print("Hello how are you!\n");
      while(1){
          if(getkey() != 0){
              print("key was pressed\n");
          }
      }
      return 0;
  }
  ```

## Test impact

Tests 08, 09, 38 had EIP-range upper bounds that assumed the user binary ended at `0x400100`. Linking `stdlib.elf` into `blank.elf` adds `start.o` + `samos.o` so the binary now stretches to ~0x401040. The ranges in those three tests are widened to `[0x400000, 0x402000]`.

## Note on file name

The book builds the stdlib file as `peachos.asm`/`peachos.h`. Per project convention (no mention of any other project), we use `samos.asm`/`samos.h`.
