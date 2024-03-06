/* aarch64 user programs: the layout of the x86 linker-elf.ld
 * files, two PT_LOAD segments, code and constants (R X), then
 * data and .bss (R W), each on its own page at 0x400000 and up. The
 * kernel maps each with its p_flags, refuses W+X, and zero-fills .bss.
 * Linked with -z max-page-size=0x1000.
 */
ENTRY(_start)
OUTPUT_FORMAT(elf64-littleaarch64)

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
        *(.rodata .rodata.*)
    } :text

    . = ALIGN(4096);

    .data : ALIGN(16)
    {
        *(.data .data.*)
        *(.got .got.*)
    } :data

    .bss : ALIGN(16)
    {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.comment)
        *(.note*)
    }
}
