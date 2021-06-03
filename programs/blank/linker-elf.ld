/* Ch 123: produce an ELF executable alongside the flat blank.bin so the
   upcoming ELF loader (Ch 124+) has something to load before we delete
   the flat-binary path. Same VMA / section ordering as linker.ld; only
   OUTPUT_FORMAT differs. */
ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
SECTIONS
{
    . = 0x400000;

    .text : ALIGN(4096)
    {
        *(.text)
    }

    .asm : ALIGN(4096)
    {
        *(.asm)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata)
    }

    .data : ALIGN(4096)
    {
        *(.data)
    }

    .bss : ALIGN(4096)
    {
        *(COMMON)
        *(.bss)
    }
}
