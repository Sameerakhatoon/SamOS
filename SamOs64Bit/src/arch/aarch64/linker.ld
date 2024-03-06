/* aarch64 kernel image layout.
 *
 * QEMU's -kernel loads an arm64 Image with text_offset 0 at RAM base
 * + 2 MiB (0x40200000 on -machine virt). objcopy -O binary keeps
 * .text through .data; .bss and the boot stack are not in the file,
 * image_size in the header covers them.
 *
 * Linked at KERNEL_VBASE (mmu.h), the top 2 GiB, whatever the
 * load address: boot.S and mmu_early run there with the MMU off using
 * only PC-relative addressing, then the MMU maps this VA to the load
 * address. Text, rodata and data start on their own pages for W^X,
 * in three segments (R X, R, R W); a guard page sits below the stack.
 */
OUTPUT_ARCH(aarch64)
ENTRY(_head)

PHDRS
{
    text   PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0xffffffff80000000;
    _head = .;

    .text : {
        KEEP(*(.text.head))
        *(.text .text.*)
    } :text
    . = ALIGN(4096);
    _text_end = .;

    .rodata : { *(.rodata .rodata.*) } :rodata
    /* (instruction, fixup) pairs of the user accesses (uaccess.S) */
    __ex_table : ALIGN(8) {
        __ex_table_start = .;
        KEEP(*(__ex_table))
        __ex_table_end = .;
    } :rodata
    . = ALIGN(4096);
    _rodata_end = .;

    .data : { *(.data .data.*) } :data

    .bss (NOLOAD) : ALIGN(16) {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        _bss_end = .;
    } :data

    /* 64 KiB: kernel_main (-O0) and, once tasks run, every trap
       from EL0, which arch_task_return starts at the top. */
    .stack (NOLOAD) : ALIGN(4096) {
        _stack_guard = .;
        . += 0x1000;
        _stack_bottom = .;
        . += 0x10000;
        _stack_top = .;
    } :data

    _end = .;
    samos_kernel_end = .;   /* the shared code's name for it (memtest.c) */
    _kernel_size = _end - _head;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
