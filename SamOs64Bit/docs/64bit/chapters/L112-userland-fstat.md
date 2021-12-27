# Lecture 112 - userland fstat

L112 closes the userland file API set with `fstat` (command 14).

## Kernel-side

- `SYSTEM_COMMAND14_FSTAT` enum entry.
- `isr80h_command14_fstat` reads fd (item 0) and the
  `struct file_stat*` output pointer (item 1), then forwards
  to `process_fstat`.
- `process_fstat(process, fd, virt_filestat_addr)`:
  1. `process_validate_memory_or_terminate` the buffer for
     `sizeof(struct file_stat)`. The L109 walk hits the
     stack-region branch when the user passes a local.
  2. `process_virtual_address_to_physical` the validated
     buffer.
  3. Forward to the kernel `fstat(fd, phys_filestat_addr)`.
- `process_virtual_address_to_physical` is a new shorthand
  wrapper over `paging_get_physical_address`. fread (L107) and
  any future buffer-translation syscalls can route through it.

## Userland-side

- `samos.asm` adds `samos_fstat` (rdi=fd, rsi=out; rax=14).
- `samos.h` declares the trampoline.
- `stdlib/file.h` mirrors the kernel `struct file_stat` shape
  (`FILE_STAT_FLAGS flags; uint32_t filesize`) and declares
  `int fstat(int fd, struct file_stat*)`.
- `stdlib/file.c::fstat` is a one-line forwarder.

## blank.c

The L110 fread test is replaced by:

```c
struct file_stat file_stat = {0};
printf("File blank.elf opened\n");
fstat(fd, &file_stat);
printf("File size: %i\n", file_stat.filesize);
fclose(fd);
```

`fstat` is cheap; the L110 `fread(buf, 1, 512, fd)` was holding
the ATA-PIO read budget on each boot. With fstat the smoke
still confirms the L105-L112 chain (fopen + L109 validate +
L112 v2p + kernel fstat + fclose-with-the-broken-trampoline +
L105 process_close_file_handles drains the leak).

## BIOS test status

Source-only. Test verifies every new symbol exists, the v2p
helper is in place, the userland struct + decl + forwarder
land, blank.c calls fstat, and the build links.
