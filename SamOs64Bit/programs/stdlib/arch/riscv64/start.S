// The riscv64 user entry, x86_64's start.asm: c_start reads
// the arguments, runs main and exits with its status, so it does not
// come back.

    .text
    .global _start
    .type _start, @function
_start:
    call    c_start
1:  j       1b
    .size _start, . - _start
