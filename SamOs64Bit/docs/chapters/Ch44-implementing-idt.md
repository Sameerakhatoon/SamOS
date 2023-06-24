# Ch 40 - Implementing the IDT

**Book pages:** 116-122 (Part 5)
**Code added:** `src/config.h`, `src/idt/{idt.asm,idt.c,idt.h}`, `src/memory/{memory.c,memory.h}`, `kernel.c` calls `idt_init()`, `kernel.h` exposes `print`, Makefile + build.sh expanded
**Test:** existing tests still pass; IDT installation is verified indirectly by the kernel reaching its end state

## What we built

A working Interrupt Descriptor Table:

1. `src/config.h` - shared constants (`KERNEL_CODE_SELECTOR = 0x08`, `KERNEL_DATA_SELECTOR = 0x10`, `SAMOS_TOTAL_INTERRUPTS = 512`).
2. `src/idt/idt.asm` - a `idt_load` function callable from C that wraps the `lidt` instruction.
3. `src/idt/idt.h` - `struct idt_desc` (8-byte gate descriptor) and `struct idtr_desc` (IDTR loader), plus `void idt_init()`.
4. `src/idt/idt.c` - the IDT itself (a 512-entry global), `idt_set` (fills one entry), `idt_zero` (a stub divide-by-zero handler that prints), and `idt_init` (zeros the table, installs idt_zero at vector 0, calls `idt_load`).
5. `src/memory/memory.c` - a freestanding `memset`.
6. `kernel.c` - `kernel_main` now calls `idt_init()` after `terminal_initialize` and `print`.

## The IDT gate setup

`idt_set` packs a 32-bit handler address into a 8-byte descriptor:

```c
desc->offset_1  = (uint32_t)address & 0x0000FFFF;     // low 16 bits
desc->selector  = KERNEL_CODE_SELECTOR;               // 0x08
desc->zero      = 0x00;
desc->type_attr = 0xEE;                               // P=1, DPL=3, 32-bit interrupt gate
desc->offset_2  = (uint32_t)address >> 16;            // high 16 bits
```

Why `0xEE`?

```
0xEE = 1110 1110
       |||| ||||
       |||| |+++- gate type = 0xE (32-bit interrupt gate)
       ||++------ DPL = 3 (allow INT n from ring 3)
       |+-------- storage = 0 (not a task gate)
       +--------- present = 1
```

DPL=3 means user-mode code can trigger this interrupt with `INT n`. We do not have user-mode yet but this matches the book listing.

## The asm wrapper for `lidt`

`lidt` requires its operand to be in memory, not in a register. C cannot emit `lidt` directly without inline assembly. The book's approach is a thin asm wrapper:

```asm
section .asm

global idt_load

idt_load:
    push ebp
    mov ebp, esp
    mov ebx, [ebp+8]         ; first arg = pointer to idtr_desc
    lidt [ebx]
    pop ebp
    ret
```

After this, C can call `idt_load(&idtr_descriptor)` to load the IDTR.

## A divide-by-zero stub

To prove the IDT is wired up, `idt_init` installs `idt_zero` at vector 0 (the CPU's `#DE` exception). Once the IDT is loaded, executing `mov ax, 0 / div ax` anywhere in the kernel would jump to `idt_zero` which prints "Divide by zero error\n" to the VGA terminal.

We do NOT currently trigger #DE in `kernel_main`. The book's listing leaves the trigger to the reader. We follow the book verbatim.

## Why an `idt.asm` section ".asm"

The `section .asm` directive at the top of `idt.asm` places the code in a section called `.asm`. Our linker script (`linker.ld`) has a corresponding `.asm` output section that collects everything assembled to `.asm`. This keeps the small asm helpers separated from compiler-emitted `.text`, which makes alignment and protection bits easier to reason about later when we set up paging.

## Makefile additions

Three new `.o` files in `FILES`:

```makefile
./build/idt/idt.asm.o ./build/idt/idt.o ./build/memory/memory.o
```

Three new rules to build them:

```makefile
./build/idt/idt.asm.o: ./src/idt/idt.asm
	nasm -f elf -g ./src/idt/idt.asm -o ./build/idt/idt.asm.o

./build/idt/idt.o: ./src/idt/idt.c
	i686-elf-gcc $(INCLUDES) -I./src/idt $(FLAGS) -std=gnu99 -c ./src/idt/idt.c -o ./build/idt/idt.o

./build/memory/memory.o: ./src/memory/memory.c
	i686-elf-gcc $(INCLUDES) -I./src/memory $(FLAGS) -std=gnu99 -c ./src/memory/memory.c -o ./build/memory/memory.o
```

`build.sh` creates the `build/idt` and `build/memory` directories so the Makefile commands have somewhere to write their `.o` files.

## How we know the IDT actually got installed

The boot test suite still passes (kernel_main runs to completion, VGA prints "Hello world!", EIP lands inside the kernel). If `idt_init` were broken or `lidt` were called with garbage, the CPU would have triple-faulted and reset, which would have shown up as PE=0 or a missing VGA buffer.

For an explicit triggering test we would add a `mov ax, 0 / div ax` somewhere and check the VGA buffer for "Divide by zero error". Per the book's listing we are not adding the trigger yet.

## Quirks worth noting

- `print` had to be added to `kernel.h` so `idt.c` can call it. Until this chapter `print` was internal to `kernel.c`.
- `SAMOS_TOTAL_INTERRUPTS` is 512 even though the CPU has only 256 vectors. The book sets it to 512 (probably to leave room for future use) and we follow that. Vectors 256..511 are unused.
- The IDT array is in `.bss` (4 KB of zero-initialized memory). With our page-aligned linker script, this gets its own page. We could later mark this page read-only after `idt_init` if we wanted defense-in-depth against accidental IDT corruption.
