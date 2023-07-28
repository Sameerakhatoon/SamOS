ENTRY(_start)
OUTPUT_FORMAT(binary)

SECTIONS
{
    . = 1M;

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

    /* kernel.asm zeroes [_kernel_bss_start, _kernel_bss_end). */
    .bss : ALIGN(4096)
    {
        _kernel_bss_start = .;
        *(COMMON)
        *(.bss)
        _kernel_bss_end = .;
    }

    /* The boot stack occupies the top 64 KiB of the 1 MiB kernel
       window (SAMOS_BOOT_STACK_SIZE in config.h). */
    ASSERT(_kernel_bss_end <= 0x200000 - 0x10000, "kernel image + .bss overlaps the boot stack")
}
