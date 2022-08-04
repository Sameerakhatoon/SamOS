# Ch 36 - Transitioning to C

**Book pages:** 101-105 (Part 5)
**Code added:** `src/kernel.c`, `src/kernel.h`, `kernel.asm` extended, Makefile gains C compile rule
**Test:** `tests/09-kernel-main-runs.sh`

## What we built

Three pieces fit together:

1. **`src/kernel.h`** declares `void kernel_main();`. Just the prototype, so any file that wants to call kernel_main can `#include "kernel.h"`.
2. **`src/kernel.c`** defines `kernel_main` as an empty function body. Compiled with the i686-elf cross-compiler.
3. **`src/kernel.asm`** is changed to `extern kernel_main` and to `call kernel_main` just before the spin loop.

After the boot sector hands off to `_start`, the kernel reloads its segment registers, enables A20, **calls into C**, then spins.

## The Makefile gains a C rule

```makefile
FILES = ./build/kernel.asm.o ./build/kernel.o
INCLUDES = -I./src
FLAGS = -g -ffreestanding -falign-jumps -falign-functions -falign-labels \
        -falign-loops -fstrength-reduce -fomit-frame-pointer \
        -finline-functions -Wno-unused-function -fno-builtin -Werror \
        -Wno-unused-label -Wno-cpp -Wno-unused-parameter -nostdlib \
        -nostartfiles -nodefaultlibs -Wall -O0 -Iinc

./build/kernel.o: ./src/kernel.c
	i686-elf-gcc $(INCLUDES) $(FLAGS) -std=gnu99 -c ./src/kernel.c -o ./build/kernel.o
```

What each FLAG does (relevant subset):

- `-ffreestanding`: tells GCC "no hosted C runtime, no libc, no headers like stdio.h".
- `-nostdlib -nostartfiles -nodefaultlibs`: do not link any libraries or startup files. We provide everything.
- `-fno-builtin`: do not emit calls to GCC builtins like `__builtin_memcpy`. We are not linking anything that provides them.
- `-fomit-frame-pointer`: smaller code; we do not need EBP-based stack walks since we have GDB.
- `-falign-*`: pad jumps, functions, labels, and loops to natural alignment. Helps the CPU's prefetcher.
- `-Werror -Wall`: warnings are errors. Catches bugs early.
- `-O0`: no optimisation. Makes the disassembly easy to follow during early development.
- `-g`: emit DWARF debug info for GDB.

## Linking the C object with the asm object

The existing rule for `./bin/kernel.bin` works unchanged because `FILES` now contains both `./build/kernel.asm.o` and `./build/kernel.o`. `i686-elf-ld -relocatable` combines them, then `i686-elf-gcc -T linker.ld` lays the result out at 1 MiB and emits a flat binary.

## Calling convention

The kernel.asm `call kernel_main` follows the System V i386 ABI:

- Arguments pushed right-to-left onto the stack (none here).
- Caller cleans up the stack after the return (none to clean).
- EAX returns an integer (we ignore the return).

GCC compiled `kernel_main` knows the same convention, so the call and return line up.

## Why the test cares about both pieces

The test does two things:

1. Greps `i686-elf-nm build/kernelfull.o` for `kernel_main` as a text symbol. If GCC inlined it away or the linker dropped it, this fails.
2. Boots the image, asks QEMU for EIP. The bootloader, GDT setup, A20 enable, and `call kernel_main` all need to work for EIP to land inside the 0x100000 to 0x102000 range. The previous test 08 only checked we reached the kernel image at all; test 09 specifically verifies the C call site was reached.

## What changes in subsequent chapters

Now that the kernel is C, every new feature we add (VGA text driver, IDT setup, paging, FAT16, etc.) goes into new `.c` and `.h` files. The Makefile grows new entries in `FILES` for each new `.o`. Assembly is reserved for things that genuinely have to be done in assembly: interrupt entry stubs, port I/O wrappers, segment register setup.
