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
// Every other entry is unexpected until user mode and keeps
// The first boot's dump: fresh stack, print, halt.

#define FRAME_SIZE  288         // sizeof(struct trap_frame)
#define F_X30       240         // x30, then sp at 248
#define F_ELR       256         // elr, spsr
#define F_ESR       272         // esr, far

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
    vent_frame 4                // sync, EL1h
    vent_frame 5                // IRQ, EL1h
    vent_dump  6
    vent_dump  7
    vent_dump  8
    vent_dump  9
    vent_dump  10
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
    add     x2, sp, #FRAME_SIZE     // SP before the trap
    stp     x30, x2, [sp, #F_X30]
    mrs     x2, elr_el1
    mrs     x3, spsr_el1
    stp     x2, x3, [sp, #F_ELR]
    mrs     x2, esr_el1
    mrs     x3, far_el1
    stp     x2, x3, [sp, #F_ESR]
    mov     x0, sp
    bl      aarch64_trap

    // x0 = the frame to resume. Its SP is just above it.
    mov     sp, x0
    ldp     x2, x3, [sp, #F_ELR]
    msr     elr_el1, x2
    msr     spsr_el1, x3
    ldp     x2, x3, [sp, #16]
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
    adrp    x5, _stack_top
    add     x5, x5, :lo12:_stack_top
    mov     sp, x5
    mrs     x1, esr_el1
    mrs     x2, elr_el1
    mrs     x3, far_el1
    mrs     x4, spsr_el1
    bl      aarch64_exception
1:  msr     daifset, #0xf
    wfi
    b       1b
