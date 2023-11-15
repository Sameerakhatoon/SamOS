#!/usr/bin/env bash
# e2e/174-acpi-reboot.sh - acpi_reboot resets through the
# FADT reset register on q35 (0xCF9, value 6) and, with no reset
# register on pc, through port 0xCF9 directly. With -no-reboot QEMU
# exits on the reset, so the QEMU process must end by itself.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.reboot_test\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

acpi_run() {
    E2E_MACHINE=$1 e2e_qemu_args
    local i
    for i in "${!E2E_QEMU_ARGS[@]}"; do
        [ "${E2E_QEMU_ARGS[$i]}" = stdio ] && E2E_QEMU_ARGS[$i]=none
    done
    rm -f "$E2E_SERIAL"
    timeout 180 qemu-system-x86_64 "${E2E_QEMU_ARGS[@]}" < /dev/null > "$E2E_QEMU_LOG" 2>&1
}

acpi_run q35
[ $? -ne 124 ] || e2e_fail "q35: QEMU still running after 180 s"
expect_serial '^acpi: reboot via reset register$' "q35: FADT reset register"
expect_no_serial 'reboot via (port|8042|triple)' "q35: no fallback"

acpi_run pc
[ $? -ne 124 ] || e2e_fail "pc: QEMU still running after 180 s"
expect_serial '^acpi: reboot via port 0xcf9$' "pc: port 0xCF9"
expect_no_serial 'reboot via (reset register|8042|triple)' "pc: no other method"
echo "PASS: e2e/174 acpi reboot"
