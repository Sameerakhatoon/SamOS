# Lecture 48 - un-stub process_load_binary

**Source commit (PeachOS64BitCourse):** `9d0eb71`
**SamOs commit:** L48 on `module1-64bit` branch
**Regression test:** `tests64/L48-process-load-binary.sh`

L46+L47 brought FAT16 and disk into the build. `fopen`, `fstat`,
`fread`, `fclose` work now. L48 un-comments the
`process_load_binary` body so it can actually read a program
file off the FAT16 disk image.

The body is identical to the 32-bit original, with the
`PEACHOS_ALL_OK` -> `SAMOS_ALL_OK` constant rename being the
only edit. fs/file.h is re-included.

`process_load_elf` stays stubbed (elfloader still not ported).

Smoke tokens unchanged from L38 - L47. Nothing in kernel_main
calls process_load_binary at L48 - we need a few more layers
(TSS, IDT-with-callbacks, isr80h) before `process_load_switch`
can actually launch a process.
