/* Lecture 66 - tight ELF for fast PIO load.
 *
 * The original linker script used ALIGN(4096) on every section.
 * OUTPUT_FORMAT(elf64-x86-64) mirrors file offsets to VMAs, so
 * with .text at 0x400000+0 and .asm at 0x400000+4096 and so on,
 * the file ends up 2+ MiB even when the actual code is a few
 * KiB. Each sector loaded over ATA-PIO costs ~10 ms in QEMU
 * TCG (port IO emulation + DRQ polling) so 2 MiB = 40 sec
 * just to read the file.
 *
 * Tightening alignment to 16 bytes shrinks the file from
 * ~2 MiB to ~10 KiB. The user code still aligns on natural
 * boundaries; we just don't pad full pages between sections.
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
