#!/usr/bin/env bash
# e2e/162-nvme-mount-fail.sh - a failed NVMe mount leaves no
# disk behind. samos.nvme_fail_test makes the mount fail after the
# controller and its I/O queues are up, the point where the driver
# used to have the disk registered already; it then freed its private
# data and the partition scan read the disk through freed memory.
# Now the controller is stopped, its queues are freed, no disk is
# registered, and the boot goes on to the BLANK.ELF selftest.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.nvme_fail_test\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

dd if=/dev/zero of="$E2E_TMP/nvme.img" bs=1M count=4 status=none
export E2E_EXTRA_QEMU_ARGS="-drive file=$E2E_TMP/nvme.img,if=none,id=nvm,format=raw -device nvme,drive=nvm,serial=samos-e2e"

boot_and_dump 25
expect_stage_value 96 -eq 1 "PCI list contains NVMe controller (0x01:0x08)"
expect_serial '^nvme: mount failed, no disk registered$' "NVMe mount failed as forced"
expect_no_serial '^nvme: controller ready' "no NVMe disk registered"
# Marker 6 is the id + 1 of the @: disk: 3 without NVMe, 4 when an
# NVMe disk sits in the list before the IDE disk's partitions.
expect_stage_value 6 -eq 3 "disk list as without NVMe"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
expect_serial "$E2E_SENTINEL" "boot finished the BLANK.ELF selftest"
echo "PASS: e2e/162 NVMe failed mount"
