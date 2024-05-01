#!/usr/bin/env bash
# e2e/207-virtio-mmio-files.sh - the storage shell files test
# (_files_lib.sh) on a machine whose disks are virtio-blk over
# virtio-mmio (QEMU virt on aarch64): the boot disk and both data
# disks are virtio-blk-device, the keys come from virtio-keyboard-device
# (the arch hook). mkdir, write, echo >, echo >>, a long name, ls, cat,
# rm, rmdir on FAT16 and FAT32, a reboot on the same disks, then Linux
# (mtools, fsck.fat) reads what SamOS wrote. Both boots: three
# virtio-blk disks, completions by interrupt (the slot's edge SPI),
# never polled.
# e2e-requires: virtio-mmio (QEMU virt's virtio-mmio slots; x86 pc and q35 have none)
FILES_BUS=virtio-mmio
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_files_lib.sh"

mmio_serial_check() {
    local log=$1 tag=$2 id
    for id in 0 1 2; do
        tr -d '\r' < "$log" | grep -E "^virtio-blk: disk $id, [0-9]+ MiB, flush, queue [0-9]+\$" > /dev/null \
            || e2e_fail "$tag: no virtio-blk disk $id in $(basename "$log")"
    done
    [ "$(tr -d '\r' < "$log" | grep -Ec '^disk: virtio-blk completions by interrupt, GSI [0-9]+ edge active-high, vector [0-9]+$')" -eq 3 ] \
        || e2e_fail "$tag: not three virtio-blk lines by interrupt in $(basename "$log")"
    if tr -d '\r' < "$log" | grep -E 'completion interrupt missing|has no IOAPIC route|virtio-blk: request timed out' > /dev/null; then
        e2e_fail "$tag: virtio-blk fell back to polling or timed out in $(basename "$log")"
    fi
    e2e_log "$tag: virtio-blk disks 0-2 by interrupt ($(basename "$log"))"
}

files_e2e_main "virtio-mmio"
mmio_serial_check "$E2E_TMP/serial-1.log" "boot 1"
mmio_serial_check "$E2E_TMP/serial-2.log" "boot 2"
echo "PASS: e2e/207 shell files on virtio-blk over virtio-mmio, FAT16 and FAT32, across a reboot"
