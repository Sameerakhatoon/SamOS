/* One PT_LOAD segment, like blank/linker-elf.ld.
 *
 * With ALIGN(4096) per section the data and .bss land in a second
 * PT_LOAD. The kernel's ELF loader zero-fills a segment's .bss tail
 * from the FIRST segment's base (elfloader.c), so it wiped the
 * shell's code from 0x400038 up to 0x401020.
 * Keep user programs to a single segment until the loader is fixed.
 */
ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
SECTIONS
{
    . = 0x400000;

    .text : ALIGN(16)
    {
        *(.text)
    }

    .asm : ALIGN(16)
    {
        *(.asm)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata)
    }

    .data : ALIGN(16)
    {
        *(.data)
    }

    .bss : ALIGN(16)
    {
        *(COMMON)
        *(.bss)
    }
}
