#!/bin/bash
# tests/44-userspace-symbols.sh
#
# Sentinel for the user-space stdlib build artifact (Ch 127+). The two
# user programs - blank.elf and shell.elf - must link against the stdlib
# that Ch 127-145 builds up. This test checks each .elf has the major
# entry points present, which proves the Makefile chain wired up
# correctly and that the stdlib.elf relocatable was actually pulled in.
#
#   _start              Ch 127 - asm entry point that calls c_start
#   c_start             Ch 144 - C bootstrap that fetches argv and calls main
#   main                user-program entry point
#   print               Ch 128 - SYSTEM_COMMAND1_PRINT wrapper
#   samos_exit          Ch 148 - SYSTEM_COMMAND9_EXIT wrapper
#   samos_process_get_arguments  Ch 143 - argv retrieval
#   samos_terminal_readline      Ch 136 - line editor (shell.elf uses it)
#   samos_system_run             Ch 145 - shell.elf command launcher
#   strncpy             Ch 139 - stdlib string ops
#   printf              Ch 135 - stdlib printf

set -e
cd "$(dirname "$0")/.."

export PATH="$HOME/opt/cross/bin:$PATH"

./build.sh > /dev/null

check_elf() {
    local elf=$1; shift
    local syms=("$@")
    [ -f "$elf" ] || { echo "FAIL: $elf missing"; exit 1; }
    local nm_out
    nm_out=$(i686-elf-nm "$elf" 2>/dev/null)
    local missing=()
    for sym in "${syms[@]}"; do
        # Accept any defined symbol (T/t code, R/r rodata, B/b bss, D/d data,
        # W/w weak). asm-defined entry points in our build land in `.asm`
        # which nm prints as R rather than T.
        if ! echo "$nm_out" | grep -qE "[ ][tTrRbBdDwW][ ]${sym}\$"; then
            missing+=("$sym")
        fi
    done
    if [ ${#missing[@]} -ne 0 ]; then
        echo "FAIL: $elf missing symbols: ${missing[*]}"
        exit 1
    fi
}

blank_syms=(
    # Ch 127 / 128 / 144 - bootstrap + entry
    _start
    c_start
    main
    # Ch 128 / 129 / 134 / 130 / 131 / 148 / 116 - syscall wrappers
    print
    samos_exit
    samos_malloc
    samos_free
    samos_putchar
    samos_getkey
    samos_getkeyblock
    samos_process_get_arguments
    samos_process_load_start
    samos_system
    samos_system_run
    # Ch 133 / 135 - printf chain
    itoa
    printf
    # Ch 139 - stdlib memory + string helpers
    memcpy
    memcmp
    memset
    strlen
    strnlen
    strcpy
    strncpy
    strncmp
    strtok
    tolower
    isdigit
    tonumericdigit
    # Ch 142 - command argument parser
    samos_parse_command
)

shell_syms=(
    _start
    c_start
    main
    print
    samos_terminal_readline
    samos_system_run
    samos_process_load_start
    samos_parse_command
    strncpy
    strtok
    samos_getkey
    samos_getkeyblock
)

check_elf programs/blank/blank.elf "${blank_syms[@]}"
check_elf programs/shell/shell.elf "${shell_syms[@]}"
exit 0
