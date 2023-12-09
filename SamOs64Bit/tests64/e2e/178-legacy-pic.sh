#!/usr/bin/env bash
# e2e/178-legacy-pic.sh - samos.legacy_pic keeps the 8259s
# and the PIT tick (the path a machine without a MADT takes); the boot
# still reaches the BLANK.ELF selftest and the shell, and the shell's
# `reboot` (syscall 50) resets the machine, which -no-reboot turns
# into QEMU exiting. pc.
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.legacy_pic\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

E2E_MACHINE=pc
_mon() {
    shell_boot || return 0
    printf 'info pic\n'
    sleep 1
    shell_type $'reboot\n'
    local i
    for ((i = 0; i < 120; i++)); do
        if ! pgrep -f "file:$E2E_SERIAL" > /dev/null; then
            touch "$E2E_TMP/qemu-ended"
            return 0
        fi
        sleep 0.5
    done
    printf 'quit\n'
}
e2e_run_qemu 300 _mon
expect_serial '^apic: legacy 8259 and PIT tick$' "legacy path chosen"
expect_no_serial '^apic: lapic' "no LAPIC set up"
expect_serial '^disk: pata primary has no IOAPIC route, polled$' "PATA polled without the APIC"
expect_serial '^e2e: user selftest done' "BLANK.ELF selftest"
expect_serial "$SHELL_BANNER" "keyboard through the 8259"
grep -Eq 'pic0: .*irq_base=20' "$E2E_QEMU_LOG" || e2e_fail "master 8259 not at vector 0x20"
grep -Eq 'pic0: .*imr=f[0-9a-f]' "$E2E_QEMU_LOG" && e2e_log "8259 imr as expected"
grep -Eq 'pic0: .*imr=(f8|f9|fa|fb|e8|e9|ea|eb)' "$E2E_QEMU_LOG" || e2e_fail "IRQ0/IRQ1 not unmasked on the 8259"
expect_serial '^rebooting$' "reboot builtin ran"
expect_serial '^acpi: reboot via port 0xcf9$' "kernel reset through 0xCF9"
[ -e "$E2E_TMP/qemu-ended" ] || e2e_fail "QEMU still running 60 s after reboot"
echo "PASS: e2e/178 legacy pic and shell reboot"
