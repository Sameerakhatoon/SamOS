/* riscv64 kernel layout. OpenSBI fw_dynamic jumps to the
   next stage at 0x80200000 (RAM base + 2 MiB, past its own image). */
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0x80200000;
    __kernel_start = .;

    .text : {
        *(.text.boot)
        *(.text .text.*)
    }
    .rodata : ALIGN(8) {
        *(.rodata .rodata.* .srodata .srodata.*)
    }
    .data : ALIGN(8) {
        *(.data .data.* .sdata .sdata.*)
    }
    .bss : ALIGN(8) {
        __bss_start = .;
        *(.sbss .sbss.* .bss .bss.* COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment) *(.note .note.*) *(.eh_frame .eh_frame_hdr)
    }
}
