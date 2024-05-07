#!/usr/bin/env bash
# e2e/167-sched-idle-last-exit.sh - "the last process exiting no
# longer panics" and done criterion "the system stays idle when no
# process is left".
#
# The shell is the last process; `exit` ends it. The kernel must not
# panic (the book: "No processes to switch too" / "No more tasks"),
# must sit in hlt with interrupts on (QEMU `info registers`: HLT=1,
# IF set, kernel rip), and must still take a keyboard IRQ: a key with
# no process to read it is reported on COM1. aarch64's
# `info registers` has no HLT; the instruction before the pc
# (x/1wx, 0xd503207f) is the wfi the CPU waits in, with PSTATE.I clear.
# riscv64 likewise (wfi 0x10500073, sstatus.SIE set).
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"

_regs() {
    printf 'info registers\n'
    e2e_arch_has x86 && return
    sleep 1
    local pc
    pc=$(tr -d '\r' < "$E2E_QEMU_LOG" | grep -aoE '( PC=|^ pc +)[0-9a-f]+' | tail -n 1 | grep -oE '[0-9a-f]+$')
    [ -n "$pc" ] && printf 'x/1wx 0x%x\n' $((0x$pc - 4))
}

_script() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_type $'exit\n'
    e2e_wait_serial '^sched: no process left, idling$' 30
    sleep 2
    _regs
    sleep 1
    _regs
    sleep 1
    printf 'sendkey a\n'
    e2e_wait_serial "^keyboard: no process for key 'a'\$" 30
    sleep 1
    _regs
    sleep 1
    _regs
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 400 _script

tr -d '\r' < "$E2E_SERIAL" | grep -E -- "$SHELL_BANNER" > /dev/null || e2e_fail "no shell"
expect_serial '^bye$' "the shell exited"
expect_serial '^sched: no process left, idling$' "the scheduler idles"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|No processes|No more tasks' "no panic"
expect_serial "^keyboard: no process for key 'a'\$" "the idle kernel took the keyboard IRQ"

regs=$(tr -d '\r' < "$E2E_QEMU_LOG")
if ! e2e_arch_has x86; then
    # IRQs unmasked while waiting (as IF on x86): aarch64 PSTATE bit 7
    # (I) clear, riscv64 sstatus.SIE (bit 1, in QEMU's mstatus) set.
    if [ "$E2E_ARCH" = riscv64 ]; then
        wfi=0x10500073 pc='^ pc +ffffffff8' st='^ mstatus +' irq_bit=0x2 irq_on=0x2
    else
        wfi=0xd503207f pc=' PC=ffffffff8' st='PSTATE=' irq_bit=0x80 irq_on=0
    fi
    # A CPU stopped in wfi has its pc just past it.
    halted=$(printf '%s' "$regs" | grep -acE "^(0x)?[0-9a-f]+: $wfi")
    [ "$halted" -ge 1 ] || e2e_fail "CPU never stopped in wfi in 4 register dumps"
    e2e_log "in wfi in $halted of 4 register dumps: ok"
    printf '%s' "$regs" | grep -aE "$pc[0-9a-f]+" > /dev/null \
        || e2e_fail "pc is not in the kernel"
    e2e_log "pc in the kernel: ok"
    # Only the dumps whose pc follows the wfi count (each dump's x/1wx):
    # one taken inside the tick handler has IRQs masked legitimately.
    for ps in $(printf '%s\n' "$regs" | awk -v st="$st" -v wfi="^(0x)?[0-9a-f]+: $wfi" '
            match($0, st "[0-9a-f]+") { ps = substr($0, RSTART, RLENGTH); sub(/.*[= ]/, "", ps) }
            $0 ~ wfi { print ps }'); do
        [ $(( 0x$ps & irq_bit )) -eq $((irq_on)) ] || e2e_fail "IRQs masked in the idle wait (status $ps)"
    done
    e2e_log "IRQs unmasked in the idle wait: ok"
    echo "PASS: e2e/167 idle after the last process exits"
    exit 0
fi
# A snapshot can land while the idle CPU handles a tick, more often on
# a loaded host. A busy-waiting idle loop would never show HLT=1.
halted=$(printf '%s' "$regs" | grep -ao 'HLT=1' | wc -l)
[ "$halted" -ge 1 ] || e2e_fail "CPU never halted in 4 register dumps"
e2e_log "HLT=1 in $halted of 4 register dumps: ok"
printf '%s' "$regs" | grep -aE 'RIP=ffffffff8[0-9a-f]+' > /dev/null \
    || e2e_fail "rip is not in the kernel"
e2e_log "rip in the kernel: ok"
# RFLAGS bit 9 (IF): interrupts on while halted. Only the halted dumps
# count: a dump taken inside the tick handler has IF clear legitimately.
# QEMU prints RFL and HLT on the same line.
for rfl in $(printf '%s' "$regs" | grep -a 'HLT=1' | grep -aoE 'RFL=[0-9a-f]+' | cut -d= -f2); do
    [ $(( 0x$rfl & 0x200 )) -ne 0 ] || e2e_fail "IF clear while halted (RFL=$rfl)"
done
e2e_log "IF set in every halted dump: ok"
echo "PASS: e2e/167 idle after the last process exits"
