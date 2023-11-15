#!/usr/bin/env bash
# e2e/177-shell-date-shutdown.sh - the shell's `date`
# (syscall 48, the RTC) prints today's UTC date, and `shutdown`
# (syscall 49, ACPI S5) powers QEMU off: the QEMU process ends by
# itself before the monitor would quit it. q35.
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"

E2E_MACHINE=q35
day_before=$(date -u +%Y-%m-%d)
_mon() {
    shell_boot || return 0
    shell_cmd 'date'
    shell_type $'shutdown\n'
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
day_after=$(date -u +%Y-%m-%d)
expect_serial "^($day_before|$day_after) [0-9]{2}:[0-9]{2}:[0-9]{2} UTC$" "date prints today's UTC date"
expect_serial '^shutting down$' "shutdown builtin ran"
expect_serial '^acpi: shutdown via S5$' "kernel powered off through S5"
[ -e "$E2E_TMP/qemu-ended" ] || e2e_fail "QEMU still running 60 s after shutdown"
e2e_log "QEMU exited by itself after shutdown"
echo "PASS: e2e/177 shell date and shutdown"
