#!/usr/bin/env bash
# e2e/173-acpi-shutdown.sh - acpi_shutdown powers the
# machine off through the FADT PM1a control block with \_S5's
# SLP_TYP. CMDLINE.TXT samos.shutdown_test calls it at the end of the
# kernel boot. QEMU must end by itself (no monitor quit, timeout not
# reached) on pc and q35, without the QEMU-port fallback.
# e2e-requires: acpi (power-off through the FADT PM1a block and \_S5)
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.shutdown_test\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

# acpi_run <machine>: QEMU with no monitor; returns QEMU's exit status
# (124 when the timeout killed it).
acpi_run() {
    E2E_MACHINE=$1 e2e_qemu_args
    local i
    for i in "${!E2E_QEMU_ARGS[@]}"; do
        [ "${E2E_QEMU_ARGS[$i]}" = stdio ] && E2E_QEMU_ARGS[$i]=none
    done
    rm -f "$E2E_SERIAL"
    timeout 180 "$E2E_QEMU_BIN" "${E2E_QEMU_ARGS[@]}" < /dev/null > "$E2E_QEMU_LOG" 2>&1
}

for m in pc q35; do
    e2e_log "booting $m with samos.shutdown_test"
    acpi_run $m
    rc=$?
    [ $rc -ne 124 ] || e2e_fail "$m: QEMU still running after 180 s"
    expect_serial '^acpi: shutdown via S5$' "$m: shutdown through PM1a with _S5"
    expect_no_serial 'QEMU PM ports|still running' "$m: no fallback needed"
    e2e_log "$m: QEMU exited by itself (status $rc)"
done
echo "PASS: e2e/173 acpi shutdown"
