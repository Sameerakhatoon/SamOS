; Lecture 62 - 64-bit userland _start.

[BITS 64]
global _start
extern c_start

section .asm

_start:
    call c_start
    ret
