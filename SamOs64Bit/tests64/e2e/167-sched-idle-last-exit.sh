#!/usr/bin/env bash
# e2e/167-sched-idle-last-exit.sh - "the last process exiting no
# longer panics" and done criterion "the system stays idle when no
# process is left".
#
# The shell is the last process; `exit` ends it. The kernel must not
# panic (the book: "No processes to switch too" / "No more tasks"),
# must sit in hlt with interrupts on (QEMU `info registers`: HLT=1,
# IF set, kernel rip), and must still take a keyboard IRQ: a key with
# no process to read it is reported on COM1.
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"

_script() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_type $'exit\n'
    e2e_wait_serial '^sched: no process left, idling$' 30
    sleep 2
    printf 'info registers\n'
    sleep 1
    printf 'sendkey a\n'
    e2e_wait_serial "^keyboard: no process for key 'a'\$" 30
    sleep 1
    printf 'info registers\n'
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 400 _script

tr -d '\r' < "$E2E_SERIAL" | grep -Eq -- "$SHELL_BANNER" || e2e_fail "no shell"
expect_serial '^bye$' "the shell exited"
expect_serial '^sched: no process left, idling$' "the scheduler idles"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|No processes|No more tasks' "no panic"
expect_serial "^keyboard: no process for key 'a'\$" "the idle kernel took the keyboard IRQ"

regs=$(tr -d '\r' < "$E2E_QEMU_LOG")
halted=$(printf '%s' "$regs" | grep -ao 'HLT=1' | wc -l)
[ "$halted" -ge 2 ] || e2e_fail "CPU not halted in both register dumps (HLT=1 seen $halted times)"
e2e_log "HLT=1 in both register dumps: ok"
printf '%s' "$regs" | grep -aoE 'RIP=ffffffff8[0-9a-f]+' | head -n 1 | grep -q . \
    || e2e_fail "rip is not in the kernel"
e2e_log "rip in the kernel: ok"
# RFLAGS bit 9 (IF): interrupts on while halted.
rfl=$(printf '%s' "$regs" | grep -aoE 'RFL=[0-9a-f]+' | tail -n 1 | cut -d= -f2)
[ -n "$rfl" ] && [ $(( 0x$rfl & 0x200 )) -ne 0 ] || e2e_fail "IF clear while idle (RFL=$rfl)"
e2e_log "IF set while idle (RFL=$rfl): ok"
echo "PASS: e2e/167 idle after the last process exits"
