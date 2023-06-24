# Ch 84 - Kernel panic

Book Ch 82: a two-line chapter. Give the kernel a way to halt with a
message when something goes irrecoverably wrong, so the symptom shows
up as a deliberate freeze with explanation instead of a triple fault.

## What got added

- `src/kernel.h` - prototype `void panic(const char* msg);`.
- `src/kernel.c` - implementation:
  ```c
  void panic(const char* msg){
      print(msg);
      while(1){}
  }
  ```
  Print whatever the caller passed, then spin forever. No interrupt
  disable yet (next chapter), so if anything else is wired to fire it
  still might; for now this matches the book's intentionally-minimal
  shape.

## No new test

We don't have any path that currently calls `panic()`. Adding a test
would require either deliberately rigging a failure (which we don't
have a good probe for yet) or running the full kernel with a synthetic
panic call which would block the dump-and-quit pattern every other
test uses. Test 9 (kernel_main runs) already proves that the
kernel-side print path is alive; once a real caller of `panic` lands,
we'll add a focused test for the message-survives-on-screen property.

## Future use

The book uses `panic` from:

- `fs_insert_filesystem` table-full check (currently `print(...) +
  while(1)` inline; gets switched to `panic` when we do the
  consistency pass).
- The IDT exception path (Ch 85+) for division-by-zero and friends.
- The kernel heap if `kheap_init` fails.

We'll wire those callers as each chapter lands.
