# Ch 92 - Implementing the TSS

Book Ch 90. We add the TSS data structure, expose `ltr` to C,
expand the GDT to carry user code/data + TSS descriptors, and load
the TR register. After this the CPU knows where to swap stacks on a
ring-3 -> ring-0 trap.

## What got added

- `src/config.h` - `SAMOS_TOTAL_GDT_SEGMENTS = 6` (was 3): now NULL +
  kernel code + kernel data + user code + user data + TSS.
- `src/task/tss.h` - the full 104-byte 32-bit TSS layout per Intel,
  `__attribute__((packed))`. We only ever fill `esp0` + `ss0`; the
  rest stays zero. `void tss_load(int tss_segment);` prototype.
- `src/task/tss.asm` - one function, `tss_load`:
  ```asm
  push ebp
  mov ebp, esp
  mov ax, [ebp+8]
  ltr ax
  pop ebp
  ret
  ```
  `ltr` writes the Task Register; argument is the GDT byte-offset of
  the TSS descriptor.
- `Makefile` + `build.sh` - new `./build/task/tss.asm.o` and a
  `build/task` mkdir.
- `src/kernel.c`:
  - Includes `task/tss.h`.
  - File-scope `struct tss tss;` so the GDT entry can take its
    address.
  - `gdt_structured` extended from 3 to 6 entries:
    1. NULL (`type = 0x00`)
    2. Kernel code (`type = 0x9A`)
    3. Kernel data (`type = 0x92`)
    4. User code (`type = 0xF8`)   <- DPL=3
    5. User data (`type = 0xF2`)   <- DPL=3
    6. TSS (`base = &tss`, `limit = sizeof(tss)`, `type = 0xE9`)
  - In `kernel_main`, after `idt_init` and before `paging_new_4gb`:
    ```c
    memset(&tss, 0x00, sizeof(tss));
    tss.esp0 = 0x600000;
    tss.ss0  = KERNEL_DATA_SELECTOR;
    tss_load(0x28);
    ```
    `esp0` points at 6 MiB inside RAM, where the kernel stack can
    safely sit (well below our heap at 0x01000000). `ss0` reuses the
    kernel data selector at GDT offset 0x10.

## The 0x28 magic number

Each GDT descriptor is 8 bytes. With six descriptors:

- 0x00 = NULL
- 0x08 = kernel code
- 0x10 = kernel data
- 0x18 = user code
- 0x20 = user data
- 0x28 = TSS

`ltr 0x28` tells the CPU "the TSS lives in the descriptor at GDT
byte offset 0x28".

## How test 35 confirms it

`tests/35-tss-loaded.sh` boots the kernel, runs QEMU monitor's
`info registers`, and greps for `TR =0028`. If the descriptor
type byte is wrong (`ltr` rejects anything that isn't a TSS type),
if the `base` field is mangled (`ltr` validates the limit/base
combination), or if `tss_load` is never called, the TR stays at
its default (0x0000) and the test fails.

## What's not yet exercised

- `esp0` isn't actually used yet because we never invoke a syscall
  from ring 3. The TSS install just primes the pump for the
  iret-into-userland trick the book covers in upcoming chapters.
- The user code/data descriptors exist in the GDT but no segment
  register currently references them.

## Next chapters

- TSS + user descriptors in place. The next book chapter starts
  the process abstraction (PCB, page directories per process,
  loading ELF or raw binaries from disk).
- Eventually the "fake interrupt return into ring 3" chapter
  builds the synthetic interrupt frame and `iret`s into user code.
