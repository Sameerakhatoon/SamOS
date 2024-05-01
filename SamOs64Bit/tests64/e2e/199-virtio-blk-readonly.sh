#!/usr/bin/env bash
# e2e/199-virtio-blk-readonly.sh - a FAT16 volume on a read-only
# virtio-blk-pci (QEMU readonly=on offers VIRTIO_BLK_F_RO), on pc.
# The driver marks the disk read-only, the volume mounts as drive 1:,
# the shell reads a file Linux put there, a write is refused with
# "read-only", and the image is byte for byte what it was.
# e2e-requires: virtio-pci (a read-only virtio-blk-pci on pc)
E2E_MACHINE=pc
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"

export MTOOLS_SKIP_CHECK=1
ro="$E2E_TMP/ro16.img"
truncate -s 64M "$ro"
mkfs.fat -F 16 -n ROVOL "$ro" >/dev/null || e2e_fail "mkfs FAT16"
printf 'on a read-only virtio disk\n' > "$E2E_TMP/host.txt"
mcopy -i "$ro" "$E2E_TMP/host.txt" ::HOSTRO.TXT || e2e_fail "mcopy host file"
before=$(md5sum < "$ro")
E2E_EXTRA_QEMU_ARGS="-drive file=$ro,format=raw,if=none,id=ro,readonly=on -device virtio-blk-pci,drive=ro"

_ro_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'cat 1:/HOSTRO.TXT'
    shell_cmd 'echo no > 1:/NEW.TXT'
    printf 'quit\n'
}
e2e_run_qemu 300 _ro_mon

expect_serial '^virtio-blk: disk 1, 64 MiB, read-only(, flush)?, queue [0-9]+$' "the disk is read-only"
expect_serial '^on a read-only virtio disk$' "cat read the Linux file"
expect_serial '^echo: 1:/NEW\.TXT: read-only$' "the write was refused"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:|virtio-blk: request timed out' "no fault"
[ "$(md5sum < "$ro")" = "$before" ] || e2e_fail "the read-only image changed"
e2e_log "image unchanged"
echo "PASS: e2e/199 read-only virtio-blk: mounted, read, write refused, image unchanged"
