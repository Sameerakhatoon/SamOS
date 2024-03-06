// Into user mode, and the FP/SIMD half of a task's state.
//
// The kernel is built -mgeneral-regs-only, so the FP/SIMD registers
// hold the user's values from the trap until the return: task.c saves
// them right after the trap frame (arch_fpu_save) and restores them
// right before arch_task_return.

    .arch   armv8-a+fp+simd

#define F_SP        248
#define F_ELR       256         // elr, spsr

    .text
// void aarch64_task_return(struct trap_frame* f, uint64_t kernel_stack_top)
// Every register from the frame, SP_EL0 = f->sp, then eret to EL0t
// with DAIF clear. The kernel stack starts over at its top: nothing
// on it is needed any more (the task's state is all in f).
    .global aarch64_task_return
aarch64_task_return:
    msr     daifset, #0xf
    mov     sp, x1
    ldp     x2, x3, [x0, #F_ELR]
    bic     x3, x3, #0xf            // EL0t
    bic     x3, x3, #0x3c0          // D A I F clear
    msr     elr_el1, x2
    msr     spsr_el1, x3
    ldr     x2, [x0, #F_SP]
    msr     sp_el0, x2
    ldp     x2, x3, [x0, #16]
    ldp     x4, x5, [x0, #32]
    ldp     x6, x7, [x0, #48]
    ldp     x8, x9, [x0, #64]
    ldp     x10, x11, [x0, #80]
    ldp     x12, x13, [x0, #96]
    ldp     x14, x15, [x0, #112]
    ldp     x16, x17, [x0, #128]
    ldp     x18, x19, [x0, #144]
    ldp     x20, x21, [x0, #160]
    ldp     x22, x23, [x0, #176]
    ldp     x24, x25, [x0, #192]
    ldp     x26, x27, [x0, #208]
    ldp     x28, x29, [x0, #224]
    ldr     x30, [x0, #240]
    ldp     x0, x1, [x0, #0]
    eret

// void arch_fpu_save(struct arch_fpu_state* s)
// q0-q31 at 0-511, FPSR at 512, FPCR at 520.
    .global arch_fpu_save
arch_fpu_save:
    stp     q0, q1, [x0, #0]
    stp     q2, q3, [x0, #32]
    stp     q4, q5, [x0, #64]
    stp     q6, q7, [x0, #96]
    stp     q8, q9, [x0, #128]
    stp     q10, q11, [x0, #160]
    stp     q12, q13, [x0, #192]
    stp     q14, q15, [x0, #224]
    stp     q16, q17, [x0, #256]
    stp     q18, q19, [x0, #288]
    stp     q20, q21, [x0, #320]
    stp     q22, q23, [x0, #352]
    stp     q24, q25, [x0, #384]
    stp     q26, q27, [x0, #416]
    stp     q28, q29, [x0, #448]
    stp     q30, q31, [x0, #480]
    mrs     x1, fpsr
    mrs     x2, fpcr
    add     x0, x0, #512
    stp     x1, x2, [x0]
    ret

// void arch_fpu_restore(const struct arch_fpu_state* s)
    .global arch_fpu_restore
arch_fpu_restore:
    ldp     q0, q1, [x0, #0]
    ldp     q2, q3, [x0, #32]
    ldp     q4, q5, [x0, #64]
    ldp     q6, q7, [x0, #96]
    ldp     q8, q9, [x0, #128]
    ldp     q10, q11, [x0, #160]
    ldp     q12, q13, [x0, #192]
    ldp     q14, q15, [x0, #224]
    ldp     q16, q17, [x0, #256]
    ldp     q18, q19, [x0, #288]
    ldp     q20, q21, [x0, #320]
    ldp     q22, q23, [x0, #352]
    ldp     q24, q25, [x0, #384]
    ldp     q26, q27, [x0, #416]
    ldp     q28, q29, [x0, #448]
    ldp     q30, q31, [x0, #480]
    add     x0, x0, #512
    ldp     x1, x2, [x0]
    msr     fpsr, x1
    msr     fpcr, x2
    ret
