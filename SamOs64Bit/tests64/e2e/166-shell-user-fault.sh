#!/usr/bin/env bash
# e2e/166-shell-user-fault.sh - "the shell keeps running" after
# a user fault, closed in the scheduler work. The interrupt work checked it with
# BLANK.ELF only; here the faulting program is started from the shell.
# KTEST.ELF de divides by zero: the kernel kills it, the shell's
# waitpid gets 128 + 0, the prompt comes back and still runs commands.
# RISC-V and AArch64 division by zero does not trap, so there
# KTEST.ELF ud runs an undefined instruction, x86's #UD: 128 + 6.
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"

case "$E2E_ARCH" in
riscv64)
    mode=ud start='KTEST ud: illegal instruction$' at='USER FAULT: scause 0x2' status=134
    fault='^USER FAULT: scause 0x2 \(illegal instruction\) in @:/ktest.elf: sepc 0x0*4' ;;
aarch64)
    mode=ud start='KTEST ud: udf$' at='USER FAULT: undefined instruction' status=134
    fault='^USER FAULT: undefined instruction in @:/ktest.elf: ESR 0x2000000 ELR 0x4' ;;
*)
    mode=de start='KTEST de: dividing by zero$' at='USER FAULT: #DE' status=128
    fault='^USER FAULT: #DE in @:/ktest.elf: rip 0x00000000004' ;;
esac

_script() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd "ktest.elf $mode"
    sleep 1
    shell_cmd 'echo still-alive'
    shell_cmd 'hello.elf after fault'
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 600 _script

tr -d '\r' < "$E2E_SERIAL" | grep -E -- "$SHELL_BANNER" > /dev/null || e2e_fail "no shell"
expect_serial "$start" "ktest started from the shell"
expect_serial "$fault" "fault reported"
expect_serial '^process killed$' "faulting program killed"
expect_serial "^ktest.elf: exit status $status\$" "the shell saw status $status"
expect_serial '^still-alive$' "the shell runs a builtin after the fault"
expect_serial '^hello: argv\[2\]=fault$' "and starts a program"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel fault"
fault=$(serial_offset "$at")
alive=$(serial_offset 'still-alive' "$fault")
[ "$fault" -ge 0 ] && [ "$alive" -gt "$fault" ] || e2e_fail "still-alive is not after the fault"
echo "PASS: e2e/166 shell survives a user fault"
