// riscv64 entry.
//
// OpenSBI (QEMU's default -bios, fw_dynamic) runs in M-mode and jumps
// here in S-mode at 0x80200000 with the MMU off, a0 = hart id and
// a1 = the device tree. One hart runs the kernel; the others park in
// wfi until the riscv64 SMP work starts them with SBI HSM.

#include "arch/riscv64/trap.h"

    .section .text.boot, "ax"
    .globl _start
_start:
    // Interrupts stay off until riscv64_main has set up the timer and
    // the PLIC.
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

    // sscratch = 0: we are in the kernel (see trap_entry).
2:  csrw sscratch, zero
    lla t0, trap_entry
    csrw stvec, t0
    mv a0, s0
    mv a1, s1
    call riscv64_main

park:
    wfi
    j park

// The trap path (stvec direct mode: every trap lands here).
//
// sscratch is 0 while the hart runs kernel code and holds the kernel
// stack top while it runs user code, so one swap tells the two
// apart: from user, sp becomes the kernel stack; from the kernel, sp
// is put back and the frame goes below the interrupted code's stack.
// riscv64_trap returns the frame to restore, which is another
// thread's when the timer preempted (kthread.c), so the restore half
// reads everything, sp last, from that frame.
    .text
    .balign 4
    .globl trap_entry
trap_entry:
    csrrw sp, sscratch, sp
    bnez sp, 1f
    csrr sp, sscratch
1:  addi sp, sp, -TRAP_FRAME_SIZE
    sd x1, 0(sp)
    .irp n,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sd x\n, ((\n - 1) * 8)(sp)
    .endr
    csrrw t0, sscratch, zero    // the interrupted sp; now in the kernel
    sd t0, 8(sp)
    csrr t0, sepc
    sd t0, TF_SEPC(sp)
    csrr t0, sstatus
    sd t0, TF_SSTATUS(sp)
    csrr t0, scause
    sd t0, TF_SCAUSE(sp)
    csrr t0, stval
    sd t0, TF_STVAL(sp)
    mv a0, sp
    call riscv64_trap

    mv sp, a0
    ld t0, TF_SEPC(sp)
    csrw sepc, t0
    ld t0, TF_SSTATUS(sp)
    csrw sstatus, t0
    // Back to U-mode: the next trap must find this kernel stack.
    andi t0, t0, 0x100          // SSTATUS_SPP
    bnez t0, 2f
    addi t0, sp, TRAP_FRAME_SIZE
    csrw sscratch, t0
2:  ld x1, 0(sp)
    .irp n,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    ld x\n, ((\n - 1) * 8)(sp)
    .endr
    ld sp, 8(sp)
    sret

// The trap tests' faulting instructions (main.c). Each is a
// function, so the dump's sepc must equal the symbol.
    .globl trap_test_illegal, trap_test_load, trap_test_ebreak
trap_test_illegal:
    unimp
    ret
trap_test_load:
    ld a0, 0(a0)
    ret
trap_test_ebreak:
    ebreak
    ret

    .data
    .balign 4
boot_lottery:
    .word 0

    .section .bss.stack, "aw", @nobits
    .balign 16
boot_stack:
    .space 16384
boot_stack_top:
