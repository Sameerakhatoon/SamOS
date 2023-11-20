// Early EL1 exception vector table, the aarch64 analogue of
// x86's early fault dump: any exception prints its vector, ESR,
// ELR, FAR and SPSR on the console and halts. The aarch64 trap work replaces this
// with a trap_frame save and real handlers.
//
// VBAR_EL1 needs 2 KiB alignment; 16 entries of 0x80 bytes: four
// groups (current EL with SP_EL0, current EL with SP_ELx, lower EL
// AArch64, lower EL AArch32) of sync, IRQ, FIQ, SError.

    .macro  vent idx
    .balign 0x80
    mov     x0, #\idx
    b       exception_common
    .endm

    .text
    .balign 0x800
    .global aarch64_vectors
aarch64_vectors:
    vent 0
    vent 1
    vent 2
    vent 3
    vent 4
    vent 5
    vent 6
    vent 7
    vent 8
    vent 9
    vent 10
    vent 11
    vent 12
    vent 13
    vent 14
    vent 15

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
