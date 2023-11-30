// Exception test hooks (samos.fault_test=<kind>, main.c).
//
// Each takes one synchronous exception at a global *_insn label, which
// tests64/arch/aarch64-traps.sh looks up with nm to check the ELR in
// the dump. x2 and x28 carry markers so the test can check the dumped
// registers too (the three that never return may clobber x28).

    .macro  markers
    mov     x2, #0x2222
    mov     x28, #0x2828
    .endm

    .text
    .global fault_test_brk, fault_test_brk_insn
fault_test_brk:
    markers
fault_test_brk_insn:
    brk     #0x5a
    ret

    .global fault_test_undef, fault_test_undef_insn
fault_test_undef:
    markers
fault_test_undef_insn:
    udf     #0x2a
    ret

// x0 = address to load from (main.c passes a misaligned one: with the
// MMU off that is an alignment fault).
    .global fault_test_dabort, fault_test_dabort_insn
fault_test_dabort:
    markers
fault_test_dabort_insn:
    ldr     x1, [x0]
    ret

// x0 in, x0 back from the SVC handler. ELR is the next instruction.
// This one returns, so only the caller-saved x2 carries a marker.
    .global fault_test_svc, fault_test_svc_insn
fault_test_svc:
    mov     x2, #0x2222
fault_test_svc_insn:
    svc     #0x80
    ret
