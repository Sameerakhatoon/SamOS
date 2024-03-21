// The user's FP registers (f0-f31, fcsr) in and out of a
// struct arch_fpu_state (arch_impl.h): 32 doubles, then fcsr.
//
// The kernel is built without F/D, so nothing but these two touches
// the FP unit; the file asks for D on its own. An instruction on the
// FP unit needs sstatus.FS other than Off.
//
// arch_fpu_save runs right after a trap from U-mode (task_save_state),
// while sstatus.FS is still the user's. Not Dirty: the registers hold
// what the last arch_fpu_restore loaded, which the state already has,
// so nothing is stored. Dirty: store them and mark the unit
// Clean, so a second save in the same trap stores nothing.

    .option arch, +d

#define SSTATUS_FS        (3 << 13)
#define SSTATUS_FS_CLEAN  (2 << 13)

    .text
    .globl arch_fpu_save
arch_fpu_save:
    csrr t0, sstatus
    li t1, SSTATUS_FS
    and t2, t0, t1
    bne t2, t1, 1f
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fsd f\n, (\n * 8)(a0)
    .endr
    frcsr t2
    sd t2, 256(a0)
    li t1, SSTATUS_FS_CLEAN ^ SSTATUS_FS
    csrc sstatus, t1
1:  ret

// The caller (task_enter) then returns to U-mode with arch_task_return,
// which marks the unit Clean in the frame it returns with.
    .globl arch_fpu_restore
arch_fpu_restore:
    li t1, SSTATUS_FS
    csrs sstatus, t1
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fld f\n, (\n * 8)(a0)
    .endr
    ld t2, 256(a0)
    fscsr t2
    ret
