# Lecture 202 - userland calls int 0x80 in fclose

Upstream: PeachOS64BitCourse 7c0a39f.

A one-line change to `programs/stdlib/src/peachos.asm` that adds
the missing `int 0x80` to the userland `peachos_fclose` stub.

## SamOs kernel-side delta

None. SamOs's program tree does not track upstream's stdlib
commits in this branch. The kernel-side `SYSTEM_COMMAND11_FCLOSE`
already exists (L106) and dispatches correctly.

## Test

The test re-asserts that the FCLOSE syscall is registered.

## BIOS test status

Source + link. Build links.
