// The only instructions that touch user memory (task/uaccess.h,
// arch/aarch64/uaccess.c). Each user access is an unprivileged LDTRB or
// STTRB: the MMU checks it with EL0's permissions, so a kernel address,
// an unmapped page or (for a store) a read-only page faults even though
// the kernel runs at EL1, and PSTATE.PAN does not stop it. Every such
// instruction is listed in __ex_table with a fixup: trap.c resumes a
// fault there at the fixup, which returns an error instead of the
// kernel halting (the riscv64 MMU pattern). Byte at a time, as on riscv64.

    .macro ex insn, fixup
    .pushsection __ex_table, "a"
    .balign 8
    .quad \insn, \fixup
    .popsection
    .endm

    .text
// size_t uaccess_copy_from(void* kdst, const void* usrc, size_t n)
// size_t uaccess_copy_to(void* udst, const void* ksrc, size_t n)
// The number of bytes NOT copied (0 = all of them).
    .global uaccess_copy_from
uaccess_copy_from:
    cbz     x2, 2f
10: ldtrb   w3, [x1]
    strb    w3, [x0], #1
    add     x1, x1, #1
    subs    x2, x2, #1
    b.ne    10b
2:  mov     x0, x2
    ret
    ex      10b, 2b

    .global uaccess_copy_to
uaccess_copy_to:
    cbz     x2, 2f
1:  ldrb    w3, [x1], #1
11: sttrb   w3, [x0]
    add     x0, x0, #1
    subs    x2, x2, #1
    b.ne    1b
2:  mov     x0, x2
    ret
    ex      11b, 2b

// long uaccess_strncpy(char* kdst, const char* usrc, size_t max):
// the length when a NUL was copied within max bytes, max when none
// was, -1 on a fault.
    .global uaccess_strncpy
uaccess_strncpy:
    mov     x4, #0
1:  cmp     x4, x2
    b.eq    3f
12: ldtrb   w3, [x1]
    strb    w3, [x0], #1
    cbz     w3, 2f
    add     x1, x1, #1
    add     x4, x4, #1
    b       1b
2:  mov     x0, x4
    ret
3:  mov     x0, x2
    ret
4:  mov     x0, #-1
    ret
    ex      12b, 4b
