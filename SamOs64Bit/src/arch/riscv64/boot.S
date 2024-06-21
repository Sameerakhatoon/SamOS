// riscv64 entry.
//
// OpenSBI (QEMU's default -bios, fw_dynamic) runs in M-mode and jumps
// here in S-mode at 0x80200000 with the MMU off, a0 = hart id and
// a1 = the device tree. One hart runs the kernel; the others park in
// wfi until the riscv64 SMP work starts them with SBI HSM.

#include "arch/riscv64/trap.h"

    .section .text.boot, "ax"
    .globl _start
// The RISC-V Linux Image header (the kernel's
// Documentation/arch/riscv/boot-image-header.rst), so U-Boot's booti
// takes the image as it takes Linux's: code0 and code1 (a jump over
// the header), text_offset (2 MiB, where fw_dynamic puts the next stage
// too), image_size (the whole image, BSS and the boot pools included,
// linker.ld), flags (bit 0: little endian), version 0.2, then the
// magics, "RISCV" (deprecated) and "RSC\x05". 64 bytes. QEMU's -kernel
// loads the file flat as before and runs code0.
_start:
    .option push
    .option norvc
    j real_start
    .option pop
    .word 0
    .quad 0x200000
    .quad __kernel_size
    .quad 0
    .word 2
    .word 0
    .quad 0
    .ascii "RISCV\0\0\0"
    .ascii "RSC\x05"
    .word 0

real_start:
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

    // Page tables for the probed mode (mmu.c), then translation
    // on and a jump to the same code at its link address. The image is
    // identity mapped too, so the fetches between the satp write and
    // the jump still work; mmu_init removes that map.
2:  mv a0, s1
    call mmu_boot
    beqz a0, park
    csrw satp, a0
    sfence.vma
    ld t0, high_entry
    jr t0

high:
    // Now in the higher half: lla gives link addresses from here on.
    lla sp, boot_stack_top
    // sscratch = 0: we are in the kernel (see trap_entry).
    csrw sscratch, zero
    lla t0, trap_entry
    csrw stvec, t0
    mv a0, s0
    mv a1, s1
    call riscv64_main

park:
    wfi
    j park

// Another hart, started by sbi_hart_start (smp.c) here at the
// physical address, MMU off, a0 = its hart id, a1 = its start ticket
// (a kernel virtual address; the stack top is its first word). There
// is no identity map any more, so stvec gets the virtual hart_high
// first: once satp is on, the next fetch from this physical address
// page-faults, and the trap is the jump to the higher half.
    .balign 4
    .globl hart_entry
hart_entry:
    csrw sie, zero
    csrci sstatus, 2
    lla t0, hart_high_entry
    ld t0, 0(t0)
    csrw stvec, t0
    lla t0, riscv64_hart_satp
    ld t0, 0(t0)
    sfence.vma
    csrw satp, t0
    sfence.vma
1:  j 1b

    .balign 4
hart_high:
    sfence.vma
    csrw sscratch, zero
    lla t0, trap_entry
    csrw stvec, t0
    ld sp, 0(a1)
    call riscv64_hart_main
2:  j 2b

// The trap path (stvec direct mode: every trap lands here).
//
// sscratch is 0 while the hart runs kernel code and holds the kernel
// stack top while it runs user code, so one swap tells the two
// apart: from user, sp becomes the kernel stack; from the kernel, sp
// is put back and the frame goes below the interrupted code's stack.
// riscv64_trap returns the frame to restore, which is another
// thread's when the timer preempted (kthread.c), so the restore half
// reads everything, sp last, from that frame. The shared
// scheduler switches tasks inside task_next instead (as on x86), whose
// arch_task_return comes back in at trap_return.
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
    // From U-mode, tp is the program's; this hart's struct cpu
    // is the word at the top of its trap stack, just above the frame.
    andi t1, t0, 0x100          // SSTATUS_SPP
    bnez t1, 3f
    ld tp, TRAP_FRAME_SIZE(sp)
3:
    // A trap in the middle of copy_from_user must not run its
    // handler with user access on; sret puts SUM back from the frame.
    li t0, SSTATUS_SUM_BIT
    csrc sstatus, t0
    csrr t0, scause
    sd t0, TF_SCAUSE(sp)
    csrr t0, stval
    sd t0, TF_STVAL(sp)
    mv a0, sp
    call riscv64_trap

    // arch_task_return enters here with a0 = a frame it has put
    // at the top of this hart's trap stack.
    .globl trap_return
trap_return:
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
    .globl trap_test_illegal, trap_test_load, trap_test_ebreak, trap_test_store
trap_test_illegal:
    unimp
    ret
trap_test_load:
    ld a0, 0(a0)
    ret
trap_test_ebreak:
    ebreak
    ret
// The MMU fault tests' store.
trap_test_store:
    sd zero, 0(a0)
    ret

    .section .rodata
    .balign 8
high_entry:
    .quad high
hart_high_entry:
    .quad hart_high

    .data
    .balign 4
boot_lottery:
    .word 0

    // Its own section, placed after the page-table pool (linker.ld).
    .section .boot_stack, "aw", @nobits
    .balign 16
boot_stack:
    .space 16384
boot_stack_top:
