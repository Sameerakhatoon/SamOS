// The only instructions that touch user memory. Each user
// load or store is listed in __ex_table with a fixup, so a fault there
// (unmapped, read-only, or no U bit) returns an error to uaccess.c
// instead of halting (trap.c's fixup). The caller sets SUM around the
// call. Byte at a time: simple, and plenty until syscalls move bulk
// data.

    .macro ex insn, fixup
    .pushsection __ex_table, "a"
    .balign 8
    .quad \insn, \fixup
    .popsection
    .endm

    .text
// size_t uaccess_copy_bytes(void* dst, const void* src, size_t n):
// the number of bytes NOT copied (0 = all of them).
    .globl uaccess_copy_bytes
uaccess_copy_bytes:
    beqz a2, 2f
1:
10: lb t0, 0(a1)
11: sb t0, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    bnez a2, 1b
2:  mv a0, a2
    ret
    ex 10b, 2b
    ex 11b, 2b

// long uaccess_strncpy_bytes(char* dst, const char* src, size_t max):
// the length when a NUL was copied within max bytes, max when none
// was, -1 on a fault.
    .globl uaccess_strncpy_bytes
uaccess_strncpy_bytes:
    li t1, 0
1:  beq t1, a2, 3f
20: lb t0, 0(a1)
    sb t0, 0(a0)
    beqz t0, 2f
    addi a0, a0, 1
    addi a1, a1, 1
    addi t1, t1, 1
    j 1b
2:  mv a0, t1
    ret
3:  mv a0, a2
    ret
4:  li a0, -1
    ret
    ex 20b, 4b
