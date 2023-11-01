// riscv64 entry.
//
// OpenSBI (QEMU's default -bios, fw_dynamic) runs in M-mode and jumps
// here in S-mode at 0x80200000 with the MMU off, a0 = hart id and
// a1 = the device tree. One hart runs the kernel; the others park in
// wfi until the riscv64 SMP work starts them with SBI HSM.

    .section .text.boot, "ax"
    .globl _start
_start:
    // Interrupts stay off until the riscv64 trap work.
    csrw sie, zero
    csrci sstatus, 2

    // Boot hart lottery. OpenSBI with HSM enters one hart only, but
    // older firmware enters every hart. The flag is in .data, so
    // zeroing BSS below cannot reopen it.
    lla t0, boot_lottery
    li t1, 1
    amoswap.w t1, t1, (t0)
    bnez t1, park

    lla sp, boot_stack_top
    mv s0, a0
    mv s1, a1

    lla t0, __bss_start
    lla t1, __bss_end
1:  bgeu t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b

2:  lla t0, trap_entry
    csrw stvec, t0
    mv a0, s0
    mv a1, s1
    call riscv64_main

park:
    wfi
    j park

// Early trap: save every GPR and the trap CSRs on a stack of its own
// (the faulting sp may be the problem) and dump them. No return yet;
// Replaces this with the real trap_frame path.
    .text
    .balign 4
    .globl trap_entry
trap_entry:
    csrw sscratch, sp
    lla sp, trap_stack_top
    addi sp, sp, -288
    sd x1, 0(sp)
    .irp n,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sd x\n, ((\n - 1) * 8)(sp)
    .endr
    csrr t0, sscratch
    sd t0, 8(sp)
    csrr t0, sepc
    sd t0, 248(sp)
    csrr t0, scause
    sd t0, 256(sp)
    csrr t0, stval
    sd t0, 264(sp)
    csrr t0, sstatus
    sd t0, 272(sp)
    mv a0, sp
    call riscv64_early_trap
3:  wfi
    j 3b

    .data
    .balign 4
boot_lottery:
    .word 0

    .section .bss.stack, "aw", @nobits
    .balign 16
boot_stack:
    .space 16384
boot_stack_top:
trap_stack:
    .space 4096
trap_stack_top:
