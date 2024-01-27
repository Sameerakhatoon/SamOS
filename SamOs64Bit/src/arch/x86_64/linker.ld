ENTRY(_start)
OUTPUT_FORMAT(binary)

/* Higher-half kernel. The loader still copies
   this flat image to physical 1 MiB and jumps there, so .boot (the
   entry stub and its page tables, kernel.asm) is linked at its load
   address. Everything else runs at SAMOS_KERNEL_VMA + its physical
   address: VMA 0xFFFFFFFF80100000 and up, LMA 0x100000 and up. */
KERNEL_VMA = 0xFFFFFFFF80000000;

SECTIONS
{
    . = 1M;

    .boot :
    {
        *(.boot)
    }

    . += KERNEL_VMA;

    /* Each block below starts on a page so paging.c can give it its
       own permissions: text RX, rodata R, data and .bss RW + NX. */
    .text ALIGN(4096) : AT(ADDR(.text) - KERNEL_VMA)
    {
        _kernel_text_start = .;
        *(.text .text.*)
        *(.asm)
        _kernel_text_end = .;
    }

    .rodata ALIGN(4096) : AT(ADDR(.rodata) - KERNEL_VMA)
    {
        _kernel_rodata_start = .;
        *(.rodata .rodata.*)
        _kernel_rodata_end = .;
    }

    .data ALIGN(4096) : AT(ADDR(.data) - KERNEL_VMA)
    {
        _kernel_data_start = .;
        *(.data .data.*)
    }

    /* kernel.asm zeroes [_kernel_bss_start, _kernel_bss_end). */
    .bss ALIGN(4096) : AT(ADDR(.bss) - KERNEL_VMA)
    {
        _kernel_bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        _kernel_bss_end = .;
    }

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
        *(.note*)
    }

    /* The boot stack occupies the top 64 KiB of the 1 MiB kernel
       window (SAMOS_BOOT_STACK_SIZE in config.h). */
    ASSERT(_kernel_bss_end - KERNEL_VMA <= 0x200000 - 0x10000, "kernel image + .bss overlaps the boot stack")
    ASSERT(SIZEOF(.boot) <= 0x10000, ".boot grew past 64 KiB")

    /* End of the loaded image; memory/pfa.c reserves up to here.
       A kernel virtual address, physical + KERNEL_VMA. */
    samos_kernel_end = .;
}
