// aarch64 entry: Linux arm64 Image header, EL2 to EL1 drop,
// stack, BSS, then aarch64_main(dtb, boot_el).
//
// Boot protocol (Linux Documentation/arch/arm64/booting.rst, which
// QEMU's -kernel follows): x0 = physical address of the DTB, x1-x3 =
// 0, MMU off, D-cache off, entered at EL2 or EL1, primary CPU only
// (QEMU holds the others in PSCI).

#define SCTLR_EL1_RES1  0x30d00800      // RES1 bits; MMU, caches, alignment check off
#define HCR_EL2_RW      (1 << 31)       // EL1 is AArch64
#define SPSR_EL1H_DAIF  0x3c5           // EL1h (SP_EL1), D A I F masked
#define CPACR_FPEN      (3 << 20)       // no FP/SIMD traps at EL1 and EL0

    .section .text.head, "ax"
    .global _head_entry
_head_entry:
    // 64-byte Image header. code0 branches over it.
    b       primary_entry           // code0
    .long   0                       // code1
    .quad   0                       // text_offset: load at 2 MiB aligned base + 0
    .quad   _kernel_size            // image_size, including .bss and the stack
    .quad   0x2                     // flags: little endian, 4K pages, base near RAM start
    .quad   0                       // res2
    .quad   0                       // res3
    .quad   0                       // res4
    .ascii  "ARM\x64"               // magic
    .long   0                       // res5 (no PE/COFF header)

primary_entry:
    mov     x19, x0                 // DTB, kept in a callee-saved register
    mrs     x20, CurrentEL
    lsr     x20, x20, #2            // boot EL, passed to C for the banner
    cmp     x20, #2
    b.ne    in_el1

    // Entered at EL2 (-machine virt,virtualization=on): configure
    // EL1 and eret into it. The kernel does not use EL2 (yet).
    mov     x1, #HCR_EL2_RW
    msr     hcr_el2, x1
    mrs     x1, cnthctl_el2
    orr     x1, x1, #3              // EL1PCTEN, EL1PCEN: counter and timer at EL1
    msr     cnthctl_el2, x1
    msr     cntvoff_el2, xzr
    mov     x1, #0x33ff             // CPTR_EL2: RES1 bits, no FP trap
    msr     cptr_el2, x1
    msr     hstr_el2, xzr
    mrs     x1, midr_el1            // EL1 reads of MIDR/MPIDR see these
    msr     vpidr_el2, x1
    mrs     x1, mpidr_el1
    msr     vmpidr_el2, x1
    ldr     x1, =SCTLR_EL1_RES1
    msr     sctlr_el1, x1
    mov     x1, #SPSR_EL1H_DAIF
    msr     spsr_el2, x1
    adr     x1, in_el1
    msr     elr_el2, x1
    eret

in_el1:
    msr     daifset, #0xf
    ldr     x1, =SCTLR_EL1_RES1
    msr     sctlr_el1, x1
    mov     x1, #CPACR_FPEN
    msr     cpacr_el1, x1
    adr     x1, aarch64_vectors
    msr     vbar_el1, x1
    isb

    adrp    x1, _stack_top
    add     x1, x1, :lo12:_stack_top
    mov     sp, x1

    // Zero BSS (16-byte aligned at both ends by the linker script).
    adrp    x1, _bss_start
    add     x1, x1, :lo12:_bss_start
    adrp    x2, _bss_end
    add     x2, x2, :lo12:_bss_end
1:  cmp     x1, x2
    b.hs    2f
    stp     xzr, xzr, [x1], #16
    b       1b
2:
    mov     x0, x19
    mov     x1, x20
    bl      aarch64_main
    // aarch64_main does not return; park if it does.
halt:
    msr     daifset, #0xf
    wfi
    b       halt
