/* riscv64 kernel layout. OpenSBI fw_dynamic jumps to the
   next stage at 0x80200000 (RAM base + 2 MiB, past its own image).

   Linked in the higher half at KERNEL_VMA (mmu.h), loaded
   wherever OpenSBI puts it: boot.S and mmu_boot run at the load
   address with the MMU off, PC-relative (medany, -mno-relax). The
   W^X boundaries are page aligned: text RX, rodata R, then data,
   bss, the boot page-table pool and the boot stack RW. */
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0xffffffff80200000;
    __kernel_start = .;

    .text : {
        *(.text.boot)
        *(.text .text.*)
    }
    . = ALIGN(4096);
    __text_end = .;

    .rodata : {
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(8);
        __ex_table_start = .;
        KEEP(*(__ex_table))
        __ex_table_end = .;
    }
    . = ALIGN(4096);
    __rodata_end = .;

    .data : {
        *(.data .data.* .sdata .sdata.*)
    }
    .bss : ALIGN(8) {
        __bss_start = .;
        *(.sbss .sbss.* .bss .bss.* COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }
    . = ALIGN(4096);
    __image_end = .;

    /* Not zeroed by boot.S: pt_alloc zeroes each table it hands out. */
    .pt_pool (NOLOAD) : {
        __pt_pool_start = .;
        . += 32 * 4096;
        __pt_pool_end = .;
    }
    .boot_stack (NOLOAD) : {
        __stack_start = .;
        *(.boot_stack)
        . = ALIGN(4096);
    }

    __kernel_end = .;
    /* The shared kernel's name for it (memtest.c). */
    samos_kernel_end = .;
    /* The Image header's image_size (boot.S), a number. */
    __kernel_size = ABSOLUTE(__kernel_end - __kernel_start);

    /DISCARD/ : {
        *(.comment) *(.note .note.*) *(.eh_frame .eh_frame_hdr)
    }
}
