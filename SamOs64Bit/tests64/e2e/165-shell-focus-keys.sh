#!/usr/bin/env bash
# e2e/165-shell-focus-keys.sh - "input goes to the focused
# process", closed in the scheduler work.
#
# From the shell, `ktest.elf keys` (A) opens a window and starts
# `ktest.elf keychild B`, whose window takes the focus; both then
# block in getkey. Keys typed now must reach B only. When B exits its
# window closes, the focus returns to A's window, and the next keys
# reach A only. Each also reports the key presses its window-event
# ring delivered, in order (the ring fix: the book returned the
# newest event every time). The shell, waiting for A, gets none of
# them, and takes the keyboard back once A exits.
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"

_script() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    local before
    before=$(shell_log_size)
    shell_type $'ktest.elf keys\n'
    shell_wait_from "$before" 'KEYS A waiting' 60
    shell_wait_from "$before" 'KEYS B waiting' 60
    sleep 1
    printf 'sendkey x\n'
    shell_wait_from "$before" 'KEYS B got x' 30
    printf 'sendkey q\n'
    shell_wait_from "$before" 'KEYS B events' 30
    sleep 1
    printf 'sendkey y\n'
    shell_wait_from "$before" 'KEYS A got y' 30
    printf 'sendkey q\n'
    shell_wait_from "$before" 'ktest.elf: exit status' 30
    sleep 1
    shell_cmd 'echo after-keys'
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 600 _script

tr -d '\r' < "$E2E_SERIAL" | grep -Eq -- "$SHELL_BANNER" || e2e_fail "no shell"
expect_serial '^KEYS B got x$' "focused process B got x"
expect_serial '^KEYS B events xq$' "B's window ring: x then q, in order"
expect_serial '^KEYS A got y$' "after B exits, the focus is back on A: A got y"
expect_serial '^KEYS A events yq$' "A's window ring: y then q"
expect_no_serial '^KEYS A got x$' "A did not get the key typed into B's window"
expect_no_serial '^KEYS B got y$' "B did not get A's key"
expect_serial '^KEYS child status 0$' "A reaped B"
expect_serial '^ktest.elf: exit status 0$' "the shell waited for A"
expect_serial '^after-keys$' "the shell has the keyboard again"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT' "no fault"
echo "PASS: e2e/165 keys go to the focused process"
