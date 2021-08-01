#!/bin/bash
# tests/43-kernel-symbols.sh
#
# Symbol-presence sentinel for the major post-Ch-100 kernel pieces. None
# of these are exercised by VGA-snapshot tests directly; this test just
# asserts the linker pulled them into the final kernel image.
#
#   process_malloc            Ch 130 - user malloc syscall handler dependency
#   process_free              Ch 131 - user free syscall handler dependency
#   process_terminate         Ch 146 - process lifecycle teardown
#   process_inject_arguments  Ch 143 - argv injection used by sys cmd 7 and boot
#   isr80h_command4_malloc    Ch 130 - SYSTEM_COMMAND4_MALLOC handler
#   isr80h_command5_free      Ch 131 - SYSTEM_COMMAND5_FREE handler
#   isr80h_command6_process_load_start    Ch 138
#   isr80h_command7_invoke_system_command Ch 145
#   isr80h_command8_get_program_arguments Ch 143
#   isr80h_command9_exit      Ch 148
#   idt_handle_exception      Ch 147 - terminates faulting user process
#   idt_clock                 Ch 150 - PIT preemption driver
#   elf_load                  Ch 124 - real ELF loader entry point
#   elf_validate_loaded       Ch 122 - ELF signature / class / encoding check
#   task_next                 Ch 147/150 - task list advancement
#   paging_get_physical_address Ch 143

set -e
cd "$(dirname "$0")/.."

export PATH="$HOME/opt/cross/bin:$PATH"

./build.sh > /dev/null

required=(
    # Ch 38 / 82 - kernel-side I/O + halt
    print
    panic
    terminal_initialize
    terminal_writechar
    terminal_backspace
    terminal_scroll
    serial_init
    # Ch 40 / 42 - IDT + IO ports + interrupt-window control
    idt_init
    idt_set
    idt_register_interrupt_callback
    outb
    insb
    enable_interrupts
    disable_interrupts
    # Ch 45 / 112 / 113 - generic IRQ + callback table
    no_interrupt_handler
    interrupt_handler
    # Ch 48 - kheap
    kheap_init
    kmalloc
    kzalloc
    kfree
    # Ch 51 - paging
    enable_paging
    paging_new_4gb
    paging_free_4gb
    paging_switch
    paging_map
    paging_map_to
    paging_map_range
    paging_set
    paging_get
    paging_get_physical_address
    paging_align_address
    paging_align_to_lower_page
    paging_is_aligned
    # Ch 54 / 55 - disk
    disk_read_block
    disk_search_and_init
    disk_get
    # Ch 58 / 64 / 69 / 71 - kernel-side strings
    strlen
    strnlen
    strcpy
    strncmp
    istrncmp
    strnlen_terminator
    tolower
    isdigit
    tonumericdigit
    memcpy
    memset
    # Ch 59 - path parser
    pathparser_parse
    # Ch 60 - disk streamer
    diskstreamer_new
    diskstreamer_seek
    diskstreamer_read
    diskstreamer_close
    # Ch 63 / 70 / 73 / 76 / 78 / 80 - VFS
    fs_init
    fs_resolve
    fopen
    fread
    fseek
    fstat
    fclose
    # Ch 65 / 68 / 72 / 75 / 77 / 79 / 81 - FAT16
    fat16_init
    fat16_check_sizes
    fat16_root_dir_total
    # Ch 89 / 90 - GDT + TSS
    gdt_load
    tss_load
    gdt_structured_to_gdt
    # Ch 91 - task struct + scheduler primitives
    task_init
    task_new
    task_free
    task_current
    task_switch
    task_page
    task_page_task
    task_get_next
    task_get_stack_item
    task_save_state
    task_current_save_state
    task_next
    task_run_first_ever_task
    task_virtual_address_to_physical
    copy_string_from_task
    # Ch 92-94 / 114 / 138 / 146 - process lifecycle
    process_current
    process_get
    process_get_free_slot
    process_load
    process_load_for_slot
    process_load_switch
    process_switch
    process_switch_to_any
    process_malloc
    process_free
    process_terminate
    process_inject_arguments
    process_get_arguments
    process_map_memory
    # Ch 102 / 103 / 108 / 116 / 117 / 130 / 131 / 138 / 145 / 143 / 148 - syscalls
    isr80h_register_command
    isr80h_register_commands
    isr80h_handler
    isr80h_handle_command
    isr80h_command1_print
    isr80h_command2_getkey
    isr80h_command3_putchar
    isr80h_command4_malloc
    isr80h_command5_free
    isr80h_command6_process_load_start
    isr80h_command7_invoke_system_command
    isr80h_command8_get_program_arguments
    isr80h_command9_exit
    # Ch 110 / 111 / 115 - keyboard
    keyboard_init
    keyboard_insert
    keyboard_push
    keyboard_pop
    keyboard_set_capslock
    keyboard_get_capslock
    # Ch 121-126 / 132 - ELF
    elf_load
    elf_close
    elf_validate_loaded
    elf_header
    elf_pheader
    elf_phdr_phys_address
    # Ch 147 / 150 - exception + clock
    idt_handle_exception
    idt_clock
)

nm_out=$(i686-elf-nm build/kernelfull.o 2>/dev/null)

missing=()
for sym in "${required[@]}"; do
    # Accept T/t (code) or R/r (asm-defined functions landing in
    # nm-classified rodata via our linker .asm section).
    if ! echo "$nm_out" | grep -qE "[ ][tTrR][ ]${sym}\$"; then
        missing+=("$sym")
    fi
done

if [ ${#missing[@]} -ne 0 ]; then
    echo "FAIL: missing kernel symbols: ${missing[*]}"
    exit 1
fi
exit 0
