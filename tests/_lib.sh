#!/bin/bash
# Shared QEMU helpers. Time-independent: tests synchronize on serial
# output markers, not wall-clock guesses at boot time.
#
#   run_kernel_capture LOG [MARKER1] [MARKER2]
#     Boot the kernel into COM1=LOG. Quit as soon as MARKER1 (default
#     "entering userland", the kernel's last pre-userland banner) is
#     in the log, AND if MARKER2 is given, after MARKER2 has appeared
#     AFTER the marker1 hit. LOG accumulates everything through MARKER2.
#
#     Behavioural tests use the two-marker form: marker1 is the event
#     they care about ("CRASH-RAN"), marker2 is "the kernel kept
#     printing afterwards" (a subsequent userland token), proving the
#     kernel did not crash on the event.
#
#   run_kernel_inspect REGS LOG MARKER1 [MARKER2] -- MONITOR-CMD [...]
#     Same marker-sync as capture, but at the synchronisation point
#     also emits the supplied QEMU monitor commands and captures their
#     replies into REGS. Use MARKER1="entering userland" + "" for the
#     info-registers tests; use BS/CRASH/EXIT markers + a "kernel kept
#     going" follow-up for behavioural pmemsave inspection.
#
# Both use -no-reboot so a triple-fault kills QEMU, and a 25 s timeout
# as a wall-clock backstop in case the kernel hangs.

_wait_for_marker(){
    local log="$1"
    local marker="$2"
    local start_byte="${3:-0}"
    local i
    for i in $(seq 1 400); do
        if [ "$start_byte" = "0" ]; then
            if grep -q -- "$marker" "$log" 2>/dev/null; then
                return 0
            fi
        else
            if tail -c "+$((start_byte + 1))" "$log" 2>/dev/null | grep -q -- "$marker"; then
                return 0
            fi
        fi
        sleep 0.05
    done
    return 1
}

_qemu_drive(){
    local log="$1"; shift
    local regs="$1"; shift
    local marker1="$1"; shift
    local marker2="$1"; shift
    : > "$log"
    {
        if _wait_for_marker "$log" "$marker1"; then
            if [ -n "$marker2" ]; then
                local pos
                pos=$(stat -c%s "$log")
                _wait_for_marker "$log" "$marker2" "$pos" || true
            fi
            local cmd
            for cmd in "$@"; do
                printf '%s\n' "$cmd"
            done
        fi
        printf 'quit\n'
    } | timeout 25 qemu-system-x86_64 \
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
    local marker1="${2:-entering userland}"
    local marker2="${3:-}"
    local sink
    sink=$(mktemp)
    _qemu_drive "$log" "$sink" "$marker1" "$marker2"
    rm -f "$sink"
}

run_kernel_inspect(){
    local regs="$1"; shift
    local log="$1"; shift
    local marker1="$1"; shift
    local marker2="$1"; shift
    _qemu_drive "$log" "$regs" "$marker1" "$marker2" "$@"
}
