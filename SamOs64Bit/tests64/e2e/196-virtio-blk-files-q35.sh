#!/usr/bin/env bash
# e2e/196-virtio-blk-files-q35.sh - e2e/195 on q35, both data
# disks on virtio-blk-pci (the boot disk stays on AHCI).
# e2e-requires: virtio-pci (virtio-blk-pci on q35; e2e/207 runs the same files test over virtio-mmio)
E2E_MACHINE=q35
FILES_BUS=virtio
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_files_lib.sh"
. "$(dirname "$0")/_virtio_lib.sh"

files_e2e_main "q35/virtio-blk"
virtio_blk_serial_check "$E2E_TMP/serial-1.log" "q35 boot 1" 1 2
virtio_blk_serial_check "$E2E_TMP/serial-2.log" "q35 boot 2" 1 2
echo "PASS: e2e/196 shell files on virtio-blk (q35), FAT16 and FAT32, across a reboot"
