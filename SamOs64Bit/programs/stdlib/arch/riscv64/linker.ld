/* The riscv64 user programs' layout: x86's two PT_LOAD
 * segments (programs/blank/linker-elf.ld), code and constants R X at
 * 0x400000, then data and .bss R W from the next page. The kernel maps
 * each with its p_flags, refuses W+X, and zeroes .bss. The small
 * data sections gcc emits on RISC-V go with their big siblings.
 * Linked with -z max-page-size=0x1000 (the Makefile).
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* PF_R | PF_X */
    data PT_LOAD FLAGS(6);   /* PF_R | PF_W */
}

SECTIONS
{
    . = 0x400000;

    .text : ALIGN(16)
    {
        *(.text .text.*)
    } :text

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.* .srodata .srodata.*)
    } :text

    . = ALIGN(4096);

    .data : ALIGN(16)
    {
        *(.data .data.* .sdata .sdata.*)
        *(.got .got.*)
    } :data

    .bss : ALIGN(16)
    {
        *(.sbss .sbss.* .bss .bss.* COMMON)
    } :data

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
        *(.note*)
        *(.riscv.attributes)
    }
}
