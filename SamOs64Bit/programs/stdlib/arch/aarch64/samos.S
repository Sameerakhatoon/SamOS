// The stdlib syscall wrappers on aarch64: the x86 convention
// mapped onto svc, as Linux does on arm64:
//
//   x8         syscall number
//   x0 - x5    arguments 1-6
//   x0         result
//
// C calls these with the arguments already in x0-x5 (AAPCS64), so a
// wrapper only loads x8. The kernel preserves every register except
// x0 (the trap entry saves the whole frame, as on x86). Assembled by
// `make ARCH=aarch64`; nothing runs them before the aarch64 user-mode work.

.macro SYSCALL name, nr
    .global \name
    .type \name, %function
\name:
    mov     x8, #\nr
    svc     #0
    ret
    .size \name, . - \name
.endm

    .text
    .balign 4
#include "syscalls.inc"
