#!/usr/bin/env bash
# e2e/126-shell-serial.sh - "a usable system", driven end to end:
#
#   QEMU sendkey -> PS/2 IRQ1 -> classic driver (Shift, E0 keys)
#     -> process keyboard ring -> shell readline (quoting, backspace)
#     -> builtins / programs -> print -> COM1 (read back here)
#
# The default boot runs the BLANK.ELF selftest, which then starts
# SHELL.ELF (boot to shell). Checks: shifted symbols typed through
# the real keyboard path, quoting, backspace at column 0, the help /
# echo / ls / cat / clear builtins, an unknown command, a program
# (HELLO.ELF with arguments) that runs and gives the prompt back,
# and - from a QEMU screendump read back with screen_text.py - that
# the terminal scrolls instead of wrapping to row 0.

set -uo pipefail
. "$(dirname "$0")/_lib.sh"

here="$(cd "$(dirname "$0")" && pwd)"
font="$E2E_ROOT/assets/sysfont.bmp"
screen="$E2E_TMP/screen.ppm"
prompt_timeout=${E2E_SHELL_PROMPT_TIMEOUT:-120}
banner='SamOs v1\.0\.0 - type help'

# One character -> one QEMU sendkey name (US layout).
key_for() {
    local c="$1"
    case "$c" in
        [a-z0-9]) echo "$c" ;;
        [A-Z])    echo "shift-$(echo "$c" | tr 'A-Z' 'a-z')" ;;
        ' ')  echo spc ;;          $'\n') echo ret ;;
        $'\b') echo backspace ;;
        '-')  echo minus ;;        '_')  echo shift-minus ;;
        '=')  echo equal ;;        '+')  echo shift-equal ;;
        '.')  echo dot ;;          '>')  echo shift-dot ;;
        ',')  echo comma ;;        '<')  echo shift-comma ;;
        '/')  echo slash ;;        '?')  echo shift-slash ;;
        ';')  echo semicolon ;;    ':')  echo shift-semicolon ;;
        "'")  echo apostrophe ;;   '"')  echo shift-apostrophe ;;
        '[')  echo bracket_left ;; '{')  echo shift-bracket_left ;;
        ']')  echo bracket_right ;; '}') echo shift-bracket_right ;;
        '\')  echo backslash ;;    '|')  echo shift-backslash ;;
        '`')  echo grave_accent ;; '~')  echo shift-grave_accent ;;
        '!')  echo shift-1 ;; '@') echo shift-2 ;; '#') echo shift-3 ;;
        '$')  echo shift-4 ;; '%') echo shift-5 ;; '^') echo shift-6 ;;
        '&')  echo shift-7 ;; '*') echo shift-8 ;; '(') echo shift-9 ;;
        ')')  echo shift-0 ;;
        *) echo "unmapped key '$c'" >&2; return 1 ;;
    esac
}

# Runs inside the monitor feeder: type a string, one sendkey per char.
type_text() {
    local s="$1" i
    for ((i = 0; i < ${#s}; i++)); do
        printf 'sendkey %s\n' "$(key_for "${s:i:1}")"
        sleep 0.15
    done
}

log_size() { stat -c %s "$E2E_SERIAL" 2>/dev/null || echo 0; }

# Type a command line and wait for the next prompt after it: "> " at
# the start of a line, since help's own text contains "> " (the
# "[> file | >> file]" of echo).
run_cmd() {
    local before t=0
    before=$(log_size)
    type_text "$1"$'\n'
    while [ $t -lt 200 ]; do
        tail -c +"$((before + 1))" "$E2E_SERIAL" 2>/dev/null | tr -d '\r' | grep -q '^> ' && return 0
        sleep 0.1
        t=$((t + 1))
    done
    e2e_log "no prompt after: $1"
}

_shell_script() {
    # The selftest polls the keyboard for 15 s before it starts the
    # shell; one Enter ends that wait (an empty line is harmless if
    # the shell already has the keyboard).
    e2e_wait_serial '^e2e: user selftest done' 90 || true
    sleep 1
    printf 'sendkey ret\n'
    if ! e2e_wait_serial "$banner" "$prompt_timeout"; then
        printf 'quit\n'
        return
    fi
    sleep 1
    # Backspace on the empty line first: must not eat the prompt or
    # leave a 0x08 in the command.
    type_text $'\b\b'
    run_cmd 'echo ok-after-bs'
    run_cmd 'echo "!@#$%^&*()_+{}|:<>?~"'
    run_cmd 'echo "a b"'
    run_cmd "echo \"a b\"   'c  d' e\\ f"
    run_cmd 'help'
    run_cmd 'ls'
    run_cmd 'cat hello.txt'
    run_cmd 'nosuchprog'
    run_cmd 'hello.elf one "two three"'
    # The shell waits for this child (1.5 s asleep) before the
    # next prompt.
    run_cmd 'ktest.elf sleep 1500'
    run_cmd 'echo back-at-prompt'
    # Scrolling: clear, then print more lines than the screen holds.
    run_cmd 'clear'
    for _ in 1 2 3 4 5 6 7; do run_cmd 'help'; done
    run_cmd 'echo LAST-LINE'
    sleep 2
    printf 'screendump %s\n' "$screen"
    sleep 2
    printf 'quit\n'
}
e2e_run_qemu 600 _shell_script

tr -d '\r' < "$E2E_SERIAL" 2>/dev/null | grep -Eq -- "$banner" \
    || e2e_fail "e2e/126: no shell prompt on COM1 (boot to shell did not happen)"

fails=0
expect_line() {   # exact line in the serial log (CR stripped)
    if tr -d '\r' < "$E2E_SERIAL" | grep -qxF -- "$1"; then
        echo "  ok: $2"
    else
        echo "  FAIL: $2 (no line '$1')"; fails=$((fails + 1))
    fi
}
expect_text() {
    if tr -d '\r' < "$E2E_SERIAL" | grep -qF -- "$1"; then
        echo "  ok: $2"
    else
        echo "  FAIL: $2 (no '$1')"; fails=$((fails + 1))
    fi
}

expect_line 'ok-after-bs'                 "backspace at column 0 left the line clean"
expect_line '!@#$%^&*()_+{}|:<>?~'        "shifted symbols typed via PS/2 Shift"
expect_line 'a b'                         'echo "a b" keeps the quoted word'
expect_line 'a b c  d e f'                "double/single quotes and backslash-space"
expect_text 'cat files...'                "help lists the builtins"
expect_text 'SHELL.ELF'                   "ls shows SHELL.ELF"
hello_first=$(head -n 1 "$E2E_ROOT/hello.txt" | tr -d '\r')
expect_text "$hello_first"                "cat hello.txt prints the file"
expect_text 'nosuchprog: command not found' "unknown command reported"
expect_line 'hello: argc=3'               "hello.elf ran with its arguments"
expect_line 'hello: argv[2]=two three'    "the quoted argument reached the program"
expect_line 'back-at-prompt'              "prompt and keyboard came back after the program"
# The shell waits for the child it starts: no prompt
# between the command and the child's last line, then the status.
expect_line 'hello.elf: exit status 0'    "the shell reports hello's exit status"
expect_line 'ktest.elf: exit status 0'    "the shell reports the sleeper's exit status"
if tr -d '\r' < "$E2E_SERIAL" | awk '
        /^> ktest\.elf sleep 1500$/ { inside = 1; next }
        inside && /^SLEEP 1500 ms took/ { ok = 1; exit }
        inside && /> / { exit }
        END { exit !ok }'; then
    echo "  ok: no prompt until the sleeping child exited"
else
    echo "  FAIL: prompt came back before the child exited"; fails=$((fails + 1))
fi

if [ -s "$screen" ] && command -v python3 >/dev/null; then
    text=$(python3 "$here/screen_text.py" "$screen" "$font")
    rows=$(printf '%s\n' "$text" | wc -l)
    last=$(printf '%s\n' "$text" | grep -n . | tail -n 1)
    last_row=${last%%:*}
    if printf '%s\n' "$text" | grep -qF 'LAST-LINE' && [ "$last_row" -ge $((rows - 1)) ]; then
        echo "  ok: terminal scrolled (output reached the bottom row $last_row/$rows)"
    else
        echo "  FAIL: terminal scroll (last text row ${last_row:-none} of $rows)"
        fails=$((fails + 1))
    fi
    if printf '%s\n' "$text" | head -n 3 | grep -qF 'SamOs v1.0.0'; then
        echo "  FAIL: banner still on screen after clear + scrolling"
        fails=$((fails + 1))
    fi
else
    echo "  FAIL: no screendump"; fails=$((fails + 1))
fi

[ "$fails" -eq 0 ] || e2e_fail "e2e/126 shell over serial: $fails check(s) failed"
echo "PASS: e2e/126 shell over serial"
