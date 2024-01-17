#!/usr/bin/env bash
# Helpers for e2e tests that drive the shell over the QEMU
# monitor (sendkey) and read COM1 back. Sourced after _lib.sh. Not a
# test itself (no NN- prefix). The same steps as e2e/126.
#
#   shell_boot            (monitor script) end the BLANK.ELF keyboard
#                         probe, wait for the shell banner
#   shell_cmd <line>      (monitor script) type a line, wait for the
#                         next prompt after it
#   shell_type <text>     (monitor script) type text, one sendkey per char
#   serial_offset <text>  byte offset of the first <text> in the log
#                         after byte $2 (default 0), or -1

SHELL_BANNER='SamOs v1\.0\.0 - type help'

shell_key_for() {
    local c="$1"
    case "$c" in
        [a-z0-9]) echo "$c" ;;
        [A-Z])    echo "shift-$(echo "$c" | tr 'A-Z' 'a-z')" ;;
        ' ')  echo spc ;;          $'\n') echo ret ;;
        '-')  echo minus ;;        '.')  echo dot ;;
        '/')  echo slash ;;        '_')  echo shift-minus ;;
        ':')  echo shift-semicolon ;;  '>') echo shift-dot ;;   # Files
        '"')  echo shift-apostrophe ;; '=') echo equal ;;
        '@')  echo shift-2 ;;      # The hardware layer virtio-input
        *) echo "unmapped key '$c'" >&2; return 1 ;;
    esac
}

shell_type() {
    local s="$1" i
    for ((i = 0; i < ${#s}; i++)); do
        printf 'sendkey %s\n' "$(shell_key_for "${s:i:1}")"
        sleep 0.15
    done
}

shell_log_size() { stat -c %s "$E2E_SERIAL" 2>/dev/null || echo 0; }

# shell_wait_from <byte> <fixed string> <seconds>
shell_wait_from() {
    local from=$1 pattern=$2 limit=$3 t=0
    while [ $t -lt $((limit * 10)) ]; do
        if tail -c +"$((from + 1))" "$E2E_SERIAL" 2>/dev/null | grep -qF -- "$pattern"; then
            return 0
        fi
        sleep 0.1
        t=$((t + 1))
    done
    return 1
}

# shell_wait_prompt <byte> <seconds>: a prompt is "> " at the start of
# a line. A bare "> " also appears inside output (help prints
# "[> file | >> file]"), and matching that typed the next command
# while the previous one was still printing.
shell_wait_prompt() {
    local from=$1 limit=$2 t=0
    while [ $t -lt $((limit * 10)) ]; do
        if tail -c +"$((from + 1))" "$E2E_SERIAL" 2>/dev/null | tr -d '\r' | grep -q '^> '; then
            return 0
        fi
        sleep 0.1
        t=$((t + 1))
    done
    return 1
}

shell_boot() {
    e2e_wait_serial '^e2e: user selftest done' 120 || true
    sleep 1
    printf 'sendkey ret\n'
    e2e_wait_serial "$SHELL_BANNER" 120 || return 1
    sleep 1
}

# shell_cmd <line> [seconds]: the next prompt the shell prints after
# the command is the sentinel, as in e2e/126.
shell_cmd() {
    local before
    before=$(shell_log_size)
    shell_type "$1"$'\n'
    shell_wait_prompt "$before" "${2:-30}" || e2e_log "no prompt after: $1"
}

serial_offset() {
    local text=$1 from=${2:-0} off
    off=$(tr -d '\r' < "$E2E_SERIAL" | tail -c +"$((from + 1))" | grep -boF -- "$text" | head -n 1 | cut -d: -f1)
    if [ -z "$off" ]; then
        echo -1
    else
        echo $((from + off))
    fi
}
