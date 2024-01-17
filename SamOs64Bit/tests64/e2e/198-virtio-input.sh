#!/usr/bin/env bash
# e2e/198-virtio-input.sh - a virtio-keyboard-pci next to the PS/2
# keyboard, on q35 and pc.
#
# QEMU sends a monitor `sendkey` to the first active input handler not
# bound to a display; a virtio keyboard becomes active (moves first)
# when the driver sets DRIVER_OK. So:
#   1. -device virtio-keyboard-pci: every key the shell gets comes in
#      through virtio-input (marker 193 counts them, "virtio-input:
#      first key event"), and a shell command typed that way runs.
#   2. q35 only, the same keyboard bound to the VGA console
#      (display=vga0): sendkey skips it and goes to PS/2, and the shell
#      still works with the virtio-input driver loaded (193 stays 0).
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_virtio_lib.sh"

_vinput_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'echo typed through virtio'
    shell_cmd 'ls @:/'
    printf 'pmemsave 0x6000 %d "%s"\n' "$E2E_MARKER_REGION_SIZE" "$E2E_DUMP"
    sleep 1
    printf 'quit\n'
}

# vinput_boot <machine> <virtio|ps2>
vinput_boot() {
    local m=$1 path=$2
    E2E_MACHINE=$m
    if [ "$path" = virtio ]; then
        E2E_EXTRA_QEMU_ARGS="-device virtio-keyboard-pci"
    else
        E2E_EXTRA_QEMU_ARGS="-vga none -device VGA,id=vga0 -device virtio-keyboard-pci,display=vga0"
    fi
    rm -f "$E2E_DUMP"
    e2e_run_qemu 300 _vinput_mon
    local tag="$m/$path"
    expect_serial '^virtio-input: QEMU Virtio Keyboard, GSI [0-9]+, vector [0-9]+, keys on vector 72$' \
        "$tag: virtio-input keyboard found and routed"
    expect_serial "$SHELL_BANNER" "$tag: shell up"
    expect_serial '^typed through virtio$' "$tag: the typed command ran"
    expect_serial '^SHELL\.ELF +[0-9]+$' "$tag: ls ran"
    expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' "$tag: no fault"
    [ -s "$E2E_DUMP" ] || e2e_fail "$tag: no marker dump"
    local keys
    keys=$(virtio_count 193)
    e2e_log "$tag: key events through virtio-input: $keys"
    if [ "$path" = virtio ]; then
        expect_serial '^virtio-input: first key event$' "$tag: keys came through virtio-input"
        # 'echo typed through virtio', 'ls @:/' and the Enter keys, each
        # a press and a release, Shift for ':'.
        [ "$keys" -ge 60 ] || e2e_fail "$tag: only $keys key events through virtio-input"
    else
        expect_no_serial 'virtio-input: first key event' "$tag: no key through virtio-input"
        [ "$keys" -eq 0 ] || e2e_fail "$tag: $keys key events through virtio-input, expected PS/2 only"
    fi
}

vinput_boot q35 virtio
vinput_boot q35 ps2
vinput_boot pc virtio
echo "PASS: e2e/198 sendkey reaches the shell through virtio-input (q35, pc), and through PS/2 when virtio is bound elsewhere"
