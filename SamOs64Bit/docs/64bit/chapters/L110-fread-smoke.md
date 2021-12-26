# Lecture 110 - compile and test fread

L110 is the first end-to-end run of the L107-L109 fread path.

## Trampoline rename

`samos_read` becomes `samos_fread` everywhere
(`samos.asm`, `samos.h`, `stdlib/file.c`). The "f" prefix
matches the userland C name and stops the trampoline from
looking like a raw read syscall.

## blank.c smoke

```c
int fd = fopen("@:/blank.elf", "r");
if(fd > 0){
    char buf[512] = {0};
    printf("File blank.elf opened\n");
    fread(buf, 1, sizeof(buf), fd);
    fclose(fd);
}
```

- `buf` lives on the userland stack, so
  `process_validate_memory_or_terminate` hits the L109
  stack-region branch.
- `fread` reads 512 bytes (first ELF header chunk).
- `fclose` runs - but the L106 trampoline bug means the kernel
  never sees it. Process exit cleans the fd up via
  `process_close_file_handles`.

## kernel.c

`process_load_switch("@:/SIMPLE.BIN", &p)` becomes
`process_load_switch("@:/blank.elf", &p)`. The L62/L65/L66 ELF
path takes the rest of the load time hit but we need blank.elf
running to exercise fread.

## stdlib/file.h includes

Adds `#include <stdint.h>` next to the existing `<stddef.h>` to
match upstream. No behaviour change.

## What boots look like (when sysfont.bmp is also present)

System terminal shows: "Hello 64-bit!", then the e820 totals,
then "Loading program...", then "File blank.elf opened". After
that the program halts in `while(1)`.

## BIOS test status

Source-only. Test verifies the rename landed across asm/h/c,
blank.c calls fread with a buf[512], the kernel loads
blank.elf, and the build links.
