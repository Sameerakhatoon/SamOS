// ktest's register round trip and raw syscalls on riscv64, the
// counterpart of regs.asm (rvtest's regs.S with ktest's interface).
//
// void ktest_regs_roundtrip(uint64_t* in, uint64_t* out, uint64_t cmd)
//
// in[n] is xn for n = 1..31, except: slot 0 is a0 after the ecall (the
// result), slot 10 a0 at the ecall, slot 2 sp (not loaded: the routine
// writes its own sp there). Then f0-f31 in slots 32-63 and fcsr in 64.
// a7 = in[17] is the syscall number (cmd, which ktest.c puts there).
// Loads every register, gp and tp included, a0 last, ecall, stores them
// all to out[] the same way and copies in[10] to out[10]. Everything
// but a0 must come back unchanged: the kernel saves x1-x31 in the trap
// frame and f0-f31, fcsr in the task.

    .option arch, +d

    // ra, gp, tp and s0-s11 are the caller's: kept at 0-112(sp), out at
    // 120(sp), in at 128(sp), t0 parked at 136(sp).
    .text
    .globl ktest_regs_roundtrip
ktest_regs_roundtrip:
    addi sp, sp, -144
    sd ra, 0(sp)
    sd gp, 8(sp)
    sd tp, 16(sp)
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11
    sd s\n, (24 + \n * 8)(sp)
    .endr
    sd a1, 120(sp)
    sd a0, 128(sp)

    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fld f\n, (256 + \n * 8)(a0)
    .endr
    ld t0, 512(a0)
    fscsr t0
    sd sp, 16(a0)
    .irp n,1,3,4,5,6,7,8,9,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    ld x\n, (\n * 8)(a0)
    .endr
    ld x10, 80(a0)
    ecall

    sd t0, 136(sp)
    ld t0, 120(sp)
    sd a0, 0(t0)
    .irp n,1,2,3,4,6,7,8,9,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sd x\n, (\n * 8)(t0)
    .endr
    ld t1, 136(sp)
    sd t1, 40(t0)
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fsd f\n, (256 + \n * 8)(t0)
    .endr
    frcsr t1
    sd t1, 512(t0)
    ld t1, 128(sp)
    ld t1, 80(t1)
    sd t1, 80(t0)

    ld ra, 0(sp)
    ld gp, 8(sp)
    ld tp, 16(sp)
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11
    ld s\n, (24 + \n * 8)(sp)
    .endr
    addi sp, sp, 144
    ret

// long ktest_syscall(long cmd, long a0, long a1, long a2, long a3)
// The arguments in a0-a3, the number in a7. Returns a0.
    .globl ktest_syscall
ktest_syscall:
    mv a7, a0
    mv a0, a1
    mv a1, a2
    mv a2, a3
    mv a3, a4
    ecall
    ret

// long ktest_syscall_rsp(long cmd, unsigned long sp)
// ecall with the user sp set to `sp` (a kernel address, say), which is
// also the first argument. t0 keeps the real sp: the kernel preserves
// every register but a0.
    .globl ktest_syscall_rsp
ktest_syscall_rsp:
    mv t0, sp
    mv a7, a0
    mv a0, a1
    mv sp, a1
    ecall
    mv sp, t0
    ret
