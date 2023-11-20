/* aarch64 kernel image layout.
 *
 * QEMU's -kernel loads an arm64 Image with text_offset 0 at RAM base
 * + 2 MiB, so on -machine virt (RAM at 0x40000000) we link there.
 * The code is PC-relative (adr/adrp), so a different 2 MiB aligned
 * base would also run; only absolute pointers in data would be off.
 * objcopy -O binary keeps .text through .data; .bss and the boot
 * stack are not in the file, image_size in the header covers them.
 */
OUTPUT_ARCH(aarch64)
ENTRY(_head)

SECTIONS
{
    . = 0x40200000;
    _head = .;

    .text : {
        KEEP(*(.text.head))
        *(.text .text.*)
    }
    .rodata : ALIGN(8) { *(.rodata .rodata.*) }
    .data : ALIGN(8) { *(.data .data.*) }

    .bss (NOLOAD) : ALIGN(16) {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        _bss_end = .;
    }

    .stack (NOLOAD) : ALIGN(16) {
        . += 0x4000;
        _stack_top = .;
    }

    _end = .;
    _kernel_size = _end - _head;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
