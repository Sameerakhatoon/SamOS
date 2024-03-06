// ktest's register round trip and raw syscalls on aarch64, the
// counterpart of regs.asm.
//
// void ktest_regs_roundtrip(uint64_t* in, uint64_t* out, uint64_t cmd)
//
// in[0..30] are what x0-x30 must hold after the svc (x0 the result),
// in[31] is x0 at the svc, in[32..95] are q0-q31 (low, high), in[96]
// and in[97] FPCR and FPSR. in[8] is the syscall number (cmd, which
// ktest.c puts there). Loads every x and q register and FPCR/FPSR,
// svc #0, stores them all to out[] the same way, and copies in[31] to
// out[31]. Everything but x0 must come back unchanged: the kernel
// saves x0-x30, sp, ELR/SPSR in the trap frame and q0-q31, FPSR, FPCR
// in the task. The caller's FPCR is put back at the end.

#define Q_BASE  256
#define FP_CTL  768             // FPCR, FPSR

    .text
    .global ktest_regs_roundtrip
ktest_regs_roundtrip:
    sub     sp, sp, #192
    stp     x19, x20, [sp, #0]
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]
    stp     x29, x30, [sp, #80]
    stp     d8, d9, [sp, #96]
    stp     d10, d11, [sp, #112]
    stp     d12, d13, [sp, #128]
    stp     d14, d15, [sp, #144]
    stp     x0, x1, [sp, #160]          // in, out
    mrs     x9, fpcr
    str     x9, [sp, #176]
    add     x9, x0, #FP_CTL
    ldp     x10, x11, [x9]
    msr     fpcr, x10
    msr     fpsr, x11

    ldp     q0, q1, [x0, #Q_BASE + 0]
    ldp     q2, q3, [x0, #Q_BASE + 32]
    ldp     q4, q5, [x0, #Q_BASE + 64]
    ldp     q6, q7, [x0, #Q_BASE + 96]
    ldp     q8, q9, [x0, #Q_BASE + 128]
    ldp     q10, q11, [x0, #Q_BASE + 160]
    ldp     q12, q13, [x0, #Q_BASE + 192]
    ldp     q14, q15, [x0, #Q_BASE + 224]
    ldp     q16, q17, [x0, #Q_BASE + 256]
    ldp     q18, q19, [x0, #Q_BASE + 288]
    ldp     q20, q21, [x0, #Q_BASE + 320]
    ldp     q22, q23, [x0, #Q_BASE + 352]
    ldp     q24, q25, [x0, #Q_BASE + 384]
    ldp     q26, q27, [x0, #Q_BASE + 416]
    ldp     q28, q29, [x0, #Q_BASE + 448]
    ldp     q30, q31, [x0, #Q_BASE + 480]
    ldp     x1, x2, [x0, #8]
    ldp     x3, x4, [x0, #24]
    ldp     x5, x6, [x0, #40]
    ldp     x7, x8, [x0, #56]
    ldp     x9, x10, [x0, #72]
    ldp     x11, x12, [x0, #88]
    ldp     x13, x14, [x0, #104]
    ldp     x15, x16, [x0, #120]
    ldp     x17, x18, [x0, #136]
    ldp     x19, x20, [x0, #152]
    ldp     x21, x22, [x0, #168]
    ldp     x23, x24, [x0, #184]
    ldp     x25, x26, [x0, #200]
    ldp     x27, x28, [x0, #216]
    ldp     x29, x30, [x0, #232]
    ldr     x0, [x0, #248]
    svc     #0

    stp     x0, x1, [sp, #-16]!
    ldr     x0, [sp, #16 + 168]         // out
    stp     x2, x3, [x0, #16]
    stp     x4, x5, [x0, #32]
    stp     x6, x7, [x0, #48]
    stp     x8, x9, [x0, #64]
    stp     x10, x11, [x0, #80]
    stp     x12, x13, [x0, #96]
    stp     x14, x15, [x0, #112]
    stp     x16, x17, [x0, #128]
    stp     x18, x19, [x0, #144]
    stp     x20, x21, [x0, #160]
    stp     x22, x23, [x0, #176]
    stp     x24, x25, [x0, #192]
    stp     x26, x27, [x0, #208]
    stp     x28, x29, [x0, #224]
    str     x30, [x0, #240]
    ldp     x2, x3, [sp], #16
    stp     x2, x3, [x0, #0]
    stp     q0, q1, [x0, #Q_BASE + 0]
    stp     q2, q3, [x0, #Q_BASE + 32]
    stp     q4, q5, [x0, #Q_BASE + 64]
    stp     q6, q7, [x0, #Q_BASE + 96]
    stp     q8, q9, [x0, #Q_BASE + 128]
    stp     q10, q11, [x0, #Q_BASE + 160]
    stp     q12, q13, [x0, #Q_BASE + 192]
    stp     q14, q15, [x0, #Q_BASE + 224]
    stp     q16, q17, [x0, #Q_BASE + 256]
    stp     q18, q19, [x0, #Q_BASE + 288]
    stp     q20, q21, [x0, #Q_BASE + 320]
    stp     q22, q23, [x0, #Q_BASE + 352]
    stp     q24, q25, [x0, #Q_BASE + 384]
    stp     q26, q27, [x0, #Q_BASE + 416]
    stp     q28, q29, [x0, #Q_BASE + 448]
    stp     q30, q31, [x0, #Q_BASE + 480]
    mrs     x1, fpcr
    mrs     x2, fpsr
    add     x3, x0, #FP_CTL
    stp     x1, x2, [x3]
    ldr     x1, [sp, #160]              // in
    ldr     x2, [x1, #248]
    str     x2, [x0, #248]
    ldr     x9, [sp, #176]
    msr     fpcr, x9

    ldp     x19, x20, [sp, #0]
    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x27, x28, [sp, #64]
    ldp     x29, x30, [sp, #80]
    ldp     d8, d9, [sp, #96]
    ldp     d10, d11, [sp, #112]
    ldp     d12, d13, [sp, #128]
    ldp     d14, d15, [sp, #144]
    add     sp, sp, #192
    ret

// long ktest_syscall(long cmd, long a0, long a1, long a2, long a3)
// The arguments in x0-x3, the number in x8 (the x86 convention on svc). Returns x0.
    .global ktest_syscall
ktest_syscall:
    mov     x8, x0
    mov     x0, x1
    mov     x1, x2
    mov     x2, x3
    mov     x3, x4
    svc     #0
    ret

// long ktest_syscall_rsp(long cmd, unsigned long sp)
// svc with the user sp set to `sp` (a kernel address, say), which is
// also the first argument. x9 keeps the real sp: the kernel preserves
// every register but x0.
    .global ktest_syscall_rsp
ktest_syscall_rsp:
    mov     x9, sp
    mov     x8, x0
    mov     x0, x1
    mov     sp, x1
    svc     #0
    mov     sp, x9
    ret
