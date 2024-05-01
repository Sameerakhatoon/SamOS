#!/usr/bin/env bash
# e2e/166-shell-user-fault.sh - "the shell keeps running" after
# a user fault, closed in the scheduler work. The interrupt work checked it with
# BLANK.ELF only; here the faulting program is started from the shell.
# KTEST.ELF de divides by zero: the kernel kills it, the shell's
# waitpid gets 128 + 0, the prompt comes back and still runs commands.
# e2e-requires: x86 (x86 exception names, error codes and registers in the report; aarch64-traps.sh and aarch64-user.sh check the ESR/ELR/FAR reports)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"

_script() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'ktest.elf de'
    sleep 1
    shell_cmd 'echo still-alive'
    shell_cmd 'hello.elf after fault'
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 600 _script

tr -d '\r' < "$E2E_SERIAL" | grep -E -- "$SHELL_BANNER" > /dev/null || e2e_fail "no shell"
expect_serial 'KTEST de: dividing by zero$' "ktest started from the shell"
expect_serial '^USER FAULT: #DE in @:/ktest.elf: rip 0x00000000004' "#DE reported"
expect_serial '^process killed$' "faulting program killed"
expect_serial '^ktest.elf: exit status 128$' "the shell saw status 128 + 0"
expect_serial '^still-alive$' "the shell runs a builtin after the fault"
expect_serial '^hello: argv\[2\]=fault$' "and starts a program"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel fault"
fault=$(serial_offset 'USER FAULT: #DE')
alive=$(serial_offset 'still-alive' "$fault")
[ "$fault" -ge 0 ] && [ "$alive" -gt "$fault" ] || e2e_fail "still-alive is not after the fault"
echo "PASS: e2e/166 shell survives a user fault"
