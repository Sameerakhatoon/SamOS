#!/usr/bin/env bash
# e2e/195-virtio-blk-files-pc.sh - the storage shell files test
# (_files_lib.sh) with both data disks on virtio-blk-pci, on pc: mkdir,
# write, echo >, echo >>, a long name, ls, cat, rm, rmdir on FAT16 and
# FAT32, a reboot on the same disks, then Linux (mtools, fsck.fat)
# reads what SamOS wrote. Both boots: virtio-blk set up both disks and
# its completions came by interrupt.
# e2e-requires: virtio-pci (virtio-blk-pci on pc; e2e/207 runs the same files test over virtio-mmio)
E2E_MACHINE=pc
FILES_BUS=virtio
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_files_lib.sh"
. "$(dirname "$0")/_virtio_lib.sh"

files_e2e_main "pc/virtio-blk"
virtio_blk_serial_check "$E2E_TMP/serial-1.log" "pc boot 1" 1 2
virtio_blk_serial_check "$E2E_TMP/serial-2.log" "pc boot 2" 1 2
echo "PASS: e2e/195 shell files on virtio-blk (pc), FAT16 and FAT32, across a reboot"
