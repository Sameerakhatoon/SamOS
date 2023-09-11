# Lecture 111 - userland fseek

L111 adds command 13 (fseek) to the userland file syscall set.

## Kernel-side

- `SYSTEM_COMMAND13_FSEEK` enum entry.
- `isr80h_command13_fseek` reads fd (stack item 0), offset
  (item 1), whence (item 2) and forwards to `process_fseek`.
- `process_fseek(process, fd, offset, whence)` finds the
  matching `process_file_handle` and calls the kernel `fseek`
  with the underlying fd. Returns `-EIO` when no handle
  matches.
- `process.h` gets an `#include "fs/file.h"` to bring in
  `FILE_SEEK_MODE`.

## Userland-side

- `samos.asm::samos_fseek` trampoline: push whence, offset, fd
  (top of stack = fd = item 0); rax = 13; int 0x80; restore rsp.
- `samos.h` declares the trampoline.
- `stdlib/file.h` declares `int fseek(int fd, int offset, int whence)`.
- `stdlib/file.c::fseek` is a one-line forwarder.

The whence values are the kernel's `SEEK_SET / SEEK_CUR /
SEEK_END` enum; users get the same constants the kernel uses.

## What boots do

blank.c still does the L110 fread of the first 512 bytes; no
fseek call yet. L112 wires fstat in similarly; L113+ start
making bigger refactors.

## BIOS test status

Source-only. Test asserts every new symbol exists, the include
is added, and the build links.
