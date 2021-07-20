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
    process_malloc
    process_free
    process_terminate
    process_inject_arguments
    isr80h_command4_malloc
    isr80h_command5_free
    isr80h_command6_process_load_start
    isr80h_command7_invoke_system_command
    isr80h_command8_get_program_arguments
    isr80h_command9_exit
    idt_handle_exception
    idt_clock
    elf_load
    elf_validate_loaded
    task_next
    paging_get_physical_address
)

nm_out=$(i686-elf-nm build/kernelfull.o 2>/dev/null)

missing=()
for sym in "${required[@]}"; do
    if ! echo "$nm_out" | grep -qE "[ ][tT][ ]${sym}\$"; then
        missing+=("$sym")
    fi
done

if [ ${#missing[@]} -ne 0 ]; then
    echo "FAIL: missing kernel symbols: ${missing[*]}"
    exit 1
fi
exit 0
