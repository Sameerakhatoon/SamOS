#!/usr/bin/env bash
# e2e/164-sched-wait-status.sh - done criterion "a parent
# receives its child's exit code". KTEST.ELF parent starts
# children with syscall 7 and waits with waitpid: exit(42), main
# returning 7, a #DE (128 + 0), a #PF (128 + 14), a CPU-bound child it
# kills (128 + 9). Also: getpid differs between parent and child, a
# status pointer into the kernel is -EFAULT and does not reap the
# child, kill of a reaped pid is -ESRCH, wait with no child -ECHILD.
# e2e-requires: x86|riscv64 (the #DE child on x86, an illegal instruction on riscv64; aarch64-user.sh parent checks the same waitpid statuses)
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF parent\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

_wait_all() {
    e2e_wait_serial '^PARENT (OK|FAIL)$' 240 || true
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 400 _wait_all

expect_serial '^EXIT pid [0-9]+ code 42$' "child printed its own pid"
expect_serial '^PARENT exit\(42\) status ok 42$' "exit(42) reaches the parent"
expect_serial '^PARENT main returning 7 ok 7$' "main's return value is the status"
if [ "$E2E_ARCH" = riscv64 ]; then
    # No divide trap; an illegal instruction is x86's #UD
    expect_serial '^PARENT undefined instruction is 128 \+ 6 ok 134$' "illegal instruction child: 134"
else
    expect_serial '^PARENT divide by zero is 128 \+ 0 ok 128$' "#DE child: 128"
fi
expect_serial '^PARENT page fault is 128 \+ 14 ok 142$' "#PF child: 142"
expect_serial '^PARENT killed status is 128 \+ 9 ok 137$' "killed child: 137"
expect_serial '^PARENT waitpid into kernel memory ok -20$' "kernel status pointer refused"
expect_serial '^PARENT no child left to wait for ok -23$' "-ECHILD with no child"
expect_no_serial '^PARENT .* FAIL' "no failed check"
expect_serial '^PARENT OK$' "all waitpid checks passed"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel fault"

# The pid the child printed is the one waitpid returned.
child=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^EXIT pid \([0-9]*\) code 42$/\1/p' | head -n 1)
expect_serial "^PARENT waitpid after the refused one ok $child\$" "waitpid returned the child's getpid ($child)"
echo "PASS: e2e/164 wait and exit status"
