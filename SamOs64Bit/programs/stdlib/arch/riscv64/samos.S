// The stdlib syscall wrappers on riscv64: the x86 convention
// mapped onto ecall, as Linux does on RISC-V:
//
//   a7         syscall number
//   a0 - a5    arguments 1-6
//   a0         result
//
// C calls these with the arguments already in a0-a5 (the standard
// calling convention), so a wrapper only loads a7. The kernel steps
// sepc past the ecall and preserves every register except a0.
// Assembled by `make ARCH=riscv64`; nothing runs them before the riscv64 user-mode work.

.macro SYSCALL name, nr
    .global \name
    .type \name, @function
\name:
    li      a7, \nr
    ecall
    ret
    .size \name, . - \name
.endm

    .text
    .balign 4
#include "syscalls.inc"
