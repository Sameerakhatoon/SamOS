#!/usr/bin/env bash
# Helpers for the virtio e2e tests (195-198). Sourced after
# _lib.sh. Not a test itself.

# virtio_blk_serial_check <serial log> <tag> <disk id>...: each disk was
# set up by virtio_blk.c, its completions are routed through the IOAPIC
# (level, active high, a vector in 0x54-0x5F) and never fell back to
# polling. S-PAR - aarch64 virt: INTx through the host bridge's
# interrupt-map, one of the GIC SPIs 35-38, the INTID its line; riscv64
# virt: one of the PLIC sources 32-35, the source its line.
virtio_blk_serial_check() {
    local log=$1 tag=$2 id line='GSI [0-9]+ level active-high, vector (8[4-9]|9[0-5])'
    [ "$E2E_ARCH" = aarch64 ] && line='GSI 3[5-8] level active-high, vector 3[5-8]'
    [ "$E2E_ARCH" = riscv64 ] && line='GSI 3[2-5] level active-high, vector 3[2-5]'
    shift 2
    for id in "$@"; do
        tr -d '\r' < "$log" | grep -E "^virtio-blk: disk $id, [0-9]+ MiB, flush, queue [0-9]+\$" > /dev/null \
            || e2e_fail "$tag: no virtio-blk disk $id in $(basename "$log")"
    done
    tr -d '\r' < "$log" | grep -E "^disk: virtio-blk completions by interrupt, $line\$" > /dev/null \
        || e2e_fail "$tag: virtio-blk INTx line not routed in $(basename "$log")"
    if tr -d '\r' < "$log" | grep -E 'completion interrupt missing|has no IOAPIC route|virtio-blk: request timed out' > /dev/null; then
        e2e_fail "$tag: virtio-blk fell back to polling or timed out in $(basename "$log")"
    fi
    e2e_log "$tag: virtio-blk disks $* by interrupt ($(basename "$log"))"
}

# virtio_count <marker>: a running-count marker, 0 when never set.
virtio_count() {
    if [ "$(marker_high "$1")" = "$(expected_high)" ]; then marker_value "$1"; else echo 0; fi
}
