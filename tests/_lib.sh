#!/bin/bash
# Shared QEMU helpers. Time-independent: tests synchronize on serial
# output, not wall-clock sleeps.
#
#   run_kernel_capture LOG
#     Boot the kernel into COM1=LOG. Quit as soon as the serial mirror
#     shows "entering userland..." (the kernel's last pre-userland
#     banner). LOG then contains every kernel-init print verbatim.
#
#   run_kernel_inspect REGS LOG MONITOR-CMD [MONITOR-CMD...]
#     Like run_kernel_capture, but at the steady-state marker also
#     emits the supplied QEMU monitor commands and captures their
#     replies into REGS. Used by the info-registers / info-tlb tests.
#
# Both use -no-reboot so a triple-fault kills QEMU instead of looping
# and `timeout` as a wall-clock backstop in case the kernel hangs
# before reaching the marker.

_qemu_drive(){
    local log="$1"; shift
    local regs="$1"; shift
    : > "$log"
    {
        local i
        local saw=0
        for i in $(seq 1 200); do
            if grep -q 'entering userland' "$log" 2>/dev/null; then
                saw=1
                break
            fi
            sleep 0.05
        done
        if [ "$saw" -eq 1 ]; then
            local cmd
            for cmd in "$@"; do
                printf '%s\n' "$cmd"
            done
        fi
        printf 'quit\n'
    } | timeout 15 qemu-system-x86_64 \
            -hda bin/os.bin \
            -m 256 \
            -accel tcg \
            -display none \
            -vga std \
            -serial file:"$log" \
            -monitor stdio \
            -no-reboot \
            > "$regs" 2>&1
}

run_kernel_capture(){
    local log="$1"
    local sink
    sink=$(mktemp)
    _qemu_drive "$log" "$sink"
    rm -f "$sink"
}

run_kernel_inspect(){
    local regs="$1"; shift
    local log="$1"; shift
    _qemu_drive "$log" "$regs" "$@"
}
