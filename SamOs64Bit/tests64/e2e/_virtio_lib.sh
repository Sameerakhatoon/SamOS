#!/usr/bin/env bash
# Helpers for the virtio e2e tests (195-198). Sourced after
# _lib.sh. Not a test itself.

# virtio_blk_serial_check <serial log> <tag> <disk id>...: each disk was
# set up by virtio_blk.c, its completions are routed through the IOAPIC
# (level, active high, a vector in 0x54-0x5F) and never fell back to
# polling.
virtio_blk_serial_check() {
    local log=$1 tag=$2 id
    shift 2
    for id in "$@"; do
        tr -d '\r' < "$log" | grep -Eq "^virtio-blk: disk $id, [0-9]+ MiB, flush, queue [0-9]+\$" \
            || e2e_fail "$tag: no virtio-blk disk $id in $(basename "$log")"
    done
    tr -d '\r' < "$log" | grep -Eq '^disk: virtio-blk completions by interrupt, GSI [0-9]+ level active-high, vector (8[4-9]|9[0-5])$' \
        || e2e_fail "$tag: virtio-blk line not routed through the IOAPIC in $(basename "$log")"
    if tr -d '\r' < "$log" | grep -Eq 'completion interrupt missing|has no IOAPIC route|virtio-blk: request timed out'; then
        e2e_fail "$tag: virtio-blk fell back to polling or timed out in $(basename "$log")"
    fi
    e2e_log "$tag: virtio-blk disks $* by interrupt ($(basename "$log"))"
}

# virtio_count <marker>: a running-count marker, 0 when never set.
virtio_count() {
    if [ "$(marker_high "$1")" = "$(expected_high)" ]; then marker_value "$1"; else echo 0; fi
}
