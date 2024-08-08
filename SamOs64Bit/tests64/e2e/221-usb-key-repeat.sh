#!/usr/bin/env bash
# e2e/221-usb-key-repeat.sh - typematic repeat of a USB keyboard.
# A USB keyboard reports a held key once (SET_IDLE(0)); usb_hid.c
# repeats it as a PS/2 keyboard does after reset: after 500 ms, about
# 10.9 times a second, until it is released. q35 with i8042=off, so
# every key came through usb-kbd. Monitor `sendkey <key> <ms>` holds
# the key that long:
#   - x held 2 s, then y: one x, about 16 repeats, then y (no repeat
#     after the release, the new key typed once);
#   - Shift+z held 1.5 s: the repeats keep Shift (capitals);
#   - a normal short press (100 ms) gives one character;
#   - Caps Lock held 1.5 s toggles once (lock keys do not repeat).
# e2e-requires: xhci (qemu-xhci on PCI)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_usb_lib.sh"

_repeat_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'echo once'
    shell_type 'echo '
    printf 'sendkey x 2000\n'
    sleep 3
    shell_cmd 'y'
    shell_type 'echo '
    printf 'sendkey shift-z 1500\n'
    sleep 2.5
    shell_cmd ' shifted'
    shell_type 'echo '
    printf 'sendkey caps_lock 1500\n'
    sleep 2.5
    shell_type 'caps'
    printf 'sendkey caps_lock\n'
    sleep 0.5
    shell_cmd ' end'
    sleep 1
    e2e_pmemsave
    sleep 1
    printf 'quit\n'
}

E2E_MACHINE=q35
usb_args ""
usb_only_keyboard
e2e_run_qemu 300 _repeat_mon
usb_enum_check "q35"
expect_serial "$SHELL_BANNER" "shell up"
expect_serial '^once$' "a short press types one character"
# Not anchored: the line lands in the middle of the shell's echo.
expect_serial 'usb-hid: first key repeat$' "a held key repeats"
line=$(tr -d '\r' < "$E2E_SERIAL" | grep -E '^x+y$' | head -n 1)
[ -n "$line" ] || e2e_fail "no line of x's then y"
xs=$(( ${#line} - 1 ))
e2e_log "x held 2 s gave $xs x's (1 + about 16 repeats expected)"
[ "$xs" -ge 6 ] && [ "$xs" -le 40 ] || e2e_fail "x held 2 s gave $xs x's, not 6 to 40"
zline=$(tr -d '\r' < "$E2E_SERIAL" | grep -E '^Z+ shifted$' | head -n 1)
[ -n "$zline" ] || e2e_fail "Shift+z held: no line of capital Z's (Shift lost during the repeat?)"
zs=$(( ${#zline} - 8 ))
e2e_log "Shift+z held 1.5 s gave $zs Z's"
[ "$zs" -ge 4 ] || e2e_fail "Shift+z held 1.5 s gave only $zs Z's"
expect_serial '^CAPS end$' "Caps Lock held toggles once, not with every repeat"
[ -s "$E2E_DUMP" ] || e2e_fail "no marker dump"
keys=$(usb_count 195)
e2e_log "USB key events: $keys"
[ "$keys" -ge 60 ] || e2e_fail "only $keys USB key events"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' "no fault"
echo "PASS: e2e/221 a held USB key repeats like PS/2 (delay, rate, Shift kept, lock keys once)"
