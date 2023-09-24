/* Two PT_LOAD segments: code and constants (R X), then data
 * and .bss (R W), each starting on its own page.
 *
 * The kernel maps every segment with the permissions in p_flags and
 * refuses a segment that is both writable and executable (W^X), and
 * gives each segment fresh zeroed pages, so .bss is zero. That
 * retires the old one-segment workaround.
 *
 * The Makefile links with -z max-page-size=0x1000: a segment's file
 * offset then only has to match its address modulo 4 KiB, not 2 MiB,
 * and the file stays a few KiB (the L66 load-time concern).
 */
ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

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

    .asm : ALIGN(16)
    {
        *(.asm)
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
        *(.eh_frame)
        *(.comment)
        *(.note*)
    }
}
