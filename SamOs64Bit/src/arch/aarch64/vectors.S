// EL1 exception vector table with a trap_frame save.
//
// VBAR_EL1 needs 2 KiB alignment; 16 entries of 0x80 bytes: four
// groups (current EL with SP_EL0, current EL with SP_ELx, lower EL
// AArch64, lower EL AArch32) of sync, IRQ, FIQ, SError.
//
// The kernel runs at EL1h, so the entries it takes are 4 (sync) and
// 5 (IRQ). They push a struct trap_frame (trap.h) on the current
// stack, call aarch64_trap(frame, vector) and restore whatever frame
// it returns: the same one, or another thread's after a preemption.
// Entries 8 (sync, svc among them) and 9 (IRQ) from EL0 do the
// same on SP_EL1, which arch_task_return left at this CPU's kernel
// stack top; the frame's sp is then SP_EL0, written back before the
// eret. Every other entry is unexpected and keeps the first boot's dump: fresh
// stack, print, halt.
//
// Kernel code never uses SP_EL0 (the user's sp is in the frame and
// reloaded on every return to EL0), so the EL1 sync entry borrows it
// to hold x0 while it checks the stack: a sync exception whose frame
// would not fit above the boot stack's guard page (an overflow, or
// samos.double_fault_test's bad sp) cannot push it and would fault
// again for ever, so it goes to bad_stack instead.

//
// Several CPUs: the stack bound and the fresh stack are this
// CPU's (struct cpu through TPIDR_EL1, which is 0 until the boot CPU
// has one: then the boot stack's). FIQs are the panic stop only
// (Group 0 SGI, smp.c's arch_stop_other_cpus): their entries never
// return, and trap_entry unmasks them as soon as the frame is saved,
// so a CPU stops even inside a handler with IRQs masked.

#define FRAME_SIZE  288         // sizeof(struct trap_frame)
#define F_X30       240         // x30, then sp at 248
#define F_SP        248
#define F_ELR       256         // elr, spsr
#define F_ESR       272         // esr, far
#define VECTOR_EL0  8           // first lower-EL entry
#define CPU_STACK_BOTTOM 496    // offsetof(struct cpu, arch.stack_bottom), checked in arch.c
#define CPU_KERNEL_STACK 504    // offsetof(struct cpu, arch.kernel_stack)

    // \reg = this CPU's kernel stack field at \off, or the boot stack's
    // \sym before TPIDR_EL1 is set.
    .macro  cpu_stack reg, off, sym
    mrs     \reg, tpidr_el1
    cbz     \reg, 1f
    ldr     \reg, [\reg, #\off]
    b       2f
1:  adrp    \reg, \sym
    add     \reg, \reg, :lo12:\sym
2:
    .endm

    .macro  vent_dump idx
    .balign 0x80
    mov     x0, #\idx
    b       exception_common
    .endm

    .macro  vent_frame idx
    .balign 0x80
    sub     sp, sp, #FRAME_SIZE
    stp     x0, x1, [sp, #0]
    mov     x1, #\idx
    b       trap_entry
    .endm

    .text
    .balign 0x800
    .global aarch64_vectors
aarch64_vectors:
    vent_dump  0
    vent_dump  1
    vent_dump  2
    vent_dump  3
    .balign 0x80                // 4: sync, EL1h
    msr     sp_el0, x0
    cpu_stack x0, CPU_STACK_BOTTOM, _stack_bottom
    add     x0, x0, #FRAME_SIZE
    cmp     sp, x0
    mrs     x0, sp_el0
    b.lo    bad_stack
    sub     sp, sp, #FRAME_SIZE
    stp     x0, x1, [sp, #0]
    mov     x1, #4
    b       trap_entry
    vent_frame 5                // IRQ, EL1h
    .balign 0x80                // 6: FIQ, EL1h: the panic stop
    b       aarch64_fiq
    vent_dump  7
    vent_frame 8                // sync, EL0 (svc, faults)
    vent_frame 9                // IRQ, EL0
    .balign 0x80                // 10: FIQ, EL0
    b       aarch64_fiq
    vent_dump  11
    vent_dump  12
    vent_dump  13
    vent_dump  14
    vent_dump  15

// x0 and x1 are saved, x1 = vector, sp = the frame.
trap_entry:
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    add     x2, sp, #FRAME_SIZE     // SP before the trap: EL1's own
    cmp     x1, #VECTOR_EL0
    b.lo    1f
    mrs     x2, sp_el0              // or the user's
1:  stp     x30, x2, [sp, #F_X30]
    mrs     x2, elr_el1
    mrs     x3, spsr_el1
    stp     x2, x3, [sp, #F_ELR]
    mrs     x2, esr_el1
    mrs     x3, far_el1
    stp     x2, x3, [sp, #F_ESR]
    msr     daifclr, #1             // FIQ: the panic stop may come now
    mov     x0, sp
    bl      aarch64_trap

    // x0 = the frame to resume. Its SP is just above it; a frame
    // going back to EL0 also brings the user's SP.
    mov     sp, x0
    ldp     x2, x3, [sp, #F_ELR]
    msr     elr_el1, x2
    msr     spsr_el1, x3
    tst     x3, #0xf                // SPSR.M: EL0t is 0
    b.ne    2f
    ldr     x2, [sp, #F_SP]
    msr     sp_el0, x2
2:  ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #F_X30]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #FRAME_SIZE
    eret

exception_common:
    // Fresh stack: the fault may have been a bad SP. Nothing returns.
    cpu_stack x5, CPU_KERNEL_STACK, _stack_top
    mov     sp, x5
    mrs     x1, esr_el1
    mrs     x2, elr_el1
    mrs     x3, far_el1
    mrs     x4, spsr_el1
    bl      aarch64_exception
1:  msr     daifset, #0xf
    wfi
    b       1b

// A sync exception at EL1 with no room for its frame: report
// on a fresh stack, with the bad SP, and halt.
bad_stack:
    mov     x5, sp
    cpu_stack x0, CPU_KERNEL_STACK, _stack_top
    mov     sp, x0
    mrs     x0, esr_el1
    mrs     x1, elr_el1
    mrs     x2, far_el1
    mrs     x3, spsr_el1
    mov     x4, x5
    bl      aarch64_bad_stack
1:  msr     daifset, #0xf
    wfi
    b       1b
