# Ch 133 - itoa() in stdlib

Pure user-space C: turns an `int` into a decimal-ASCII string. No kernel call.

## What landed

- `programs/stdlib/src/stdlib.h` - prototype `char* itoa(int i);`.
- `programs/stdlib/src/stdlib.c` - body:
  ```c
  char* itoa(int i){
      static char text[12];
      int loc = 11;
      text[11] = 0;
      char neg = 1;
      if(i >= 0){ neg = 0; i = -i; }
      while(i){
          text[--loc] = '0' - (i % 10);
          i /= 10;
      }
      if(loc == 11) text[--loc] = '0';
      if(neg)       text[--loc] = '-';
      return &text[loc];
  }
  ```
- `programs/blank/blank.c` - calls `print(itoa(8763))` between the greeting and the malloc/free pair.

## Notes

- The `static char text[12]` means the buffer is reused across calls (not thread-safe, but our user world has one task).
- Works for `INT_MIN` thanks to the `i = -i` trick on the negative path: `-INT_MIN` overflows in 2's complement but the modulo math still produces the right digits when paired with the `'0' - (i % 10)` pattern.

## Test impact

None. 32/32 stays green. Visible effect: VGA now shows "8763" after the greeting on boot.
