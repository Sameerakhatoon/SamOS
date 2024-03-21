// rvtest's register round trip, riscv64's ktest regs.asm.
//
//   void rvtest_regs_ecall(uint64_t* in, uint64_t* out)
//   void rvtest_regs_spin(uint64_t* in, uint64_t* out)
//
// in[] and out[] have 65 slots: in[n] is xn (slot 0 unused, slot 2 is
// sp, which is not loaded: the routine writes its sp into in[2]), then
// f0-f31 in slots 32-63 and fcsr in slot 64. Every register but sp is
// loaded from in[], gp and tp included, a0 last since it holds in.
// Then either an ecall (a7 = in[17] is the syscall number, a0 and a1
// its arguments) or a loop that counts t6 down to 0, long enough for
// timer interrupts to preempt it. Everything comes back to out[]; the
// C side compares it with what the syscall or the loop may change.

    .option arch, +d

    .macro load_all
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
    .endm

    // t0 is parked at 128(sp) while out (120(sp)) is in t0.
    .macro store_all
    sd t0, 128(sp)
    ld t0, 120(sp)
    .irp n,1,2,3,4,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sd x\n, (\n * 8)(t0)
    .endr
    ld t1, 128(sp)
    sd t1, 40(t0)
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fsd f\n, (256 + \n * 8)(t0)
    .endr
    frcsr t1
    sd t1, 512(t0)
    .endm

    // ra, gp, tp and s0-s11 are the caller's: kept at 0-112(sp).
    .macro prologue
    addi sp, sp, -144
    sd ra, 0(sp)
    sd gp, 8(sp)
    sd tp, 16(sp)
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11
    sd s\n, (24 + \n * 8)(sp)
    .endr
    sd a1, 120(sp)
    .endm

    .macro epilogue
    ld ra, 0(sp)
    ld gp, 8(sp)
    ld tp, 16(sp)
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11
    ld s\n, (24 + \n * 8)(sp)
    .endr
    addi sp, sp, 144
    ret
    .endm

    .text
    .globl rvtest_regs_ecall
rvtest_regs_ecall:
    prologue
    load_all
    ecall
    store_all
    epilogue

    .globl rvtest_regs_spin
rvtest_regs_spin:
    prologue
    load_all
1:  addi t6, t6, -1
    bnez t6, 1b
    store_all
    epilogue

// The fault probes: one instruction each, so sepc is the symbol.
// rvtest_csr reads sstatus, which U-mode may not: an illegal
// instruction only if the program really runs in U-mode.
    .globl rvtest_load, rvtest_store, rvtest_illegal, rvtest_csr
rvtest_csr:
    csrr a0, sstatus
    ret
rvtest_load:
    ld a0, 0(a0)
    ret
rvtest_store:
    sd zero, 0(a0)
    ret
rvtest_illegal:
    .word 0
    ret
