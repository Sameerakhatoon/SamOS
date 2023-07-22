#!/usr/bin/env bash
# Shared library for end-to-end runtime tests.
#
# Each e2e test boots SamOS in QEMU under OVMF (UEFI), lets the
# kernel run for a few seconds, then uses pmemsave to dump the
# bootmarker region. Everything the kernel prints also goes to COM1,
# which QEMU writes to a per-test serial log. The library provides:
#
#   boot_and_dump <seconds>     - boot QEMU, dump 0x6000 markers, keep serial log
#   e2e_qemu_args               - fill the E2E_QEMU_ARGS array (for custom runs)
#   e2e_run_qemu <timeout> <fn> - run QEMU, piping <fn>'s output to the monitor
#   marker_high <stage>         - high 32 bits of marker[stage]
#   marker_low  <stage>         - low 32 bits of marker[stage]
#   expect_stage_reached <stage> [name]
#   expect_stage_value  <stage> <op> <expected> [name]
#   expect_serial <regex> [name]      - serial log matches an ERE
#   expect_no_serial <regex> [name]   - serial log does not match
#
# Every test gets its own temp directory (E2E_TMP), so tests can
# run in parallel (run-all.sh -j N). The disk image is attached
# with snapshot=on, so concurrent QEMUs never take a write lock on
# bin/os.img and guest writes are discarded.
#
# Knobs (environment): E2E_MACHINE (pc|q35, default pc), E2E_MEM
# (default 512M), E2E_EXTRA_QEMU_ARGS, E2E_KEEP=1 to keep E2E_TMP.
#
# Magic layout (see src/bootmarker.h):
#   bits 63..32 : 0xB007C0DE (BOOT_MARKER_MAGIC_PREFIX)
#   bits 31..24 : stage id
#   bits 23..0  : value
#
# Bash's printf cannot do 64-bit hex, so we work with the
# marker as two 32-bit halves via `od -An -v -tx4`.

set -uo pipefail

E2E_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
E2E_OS_IMG="$(cd "$E2E_ROOT/.." 2>/dev/null && pwd)/bin/os.img"
E2E_OVMF="${E2E_OVMF:-/usr/share/ovmf/OVMF.fd}"

E2E_TMP="$(mktemp -d "${TMPDIR:-/tmp}/samos-e2e.XXXXXX")"
if [ "${E2E_KEEP:-0}" != 1 ]; then
    trap 'rm -rf "$E2E_TMP"' EXIT
fi
E2E_DUMP="$E2E_TMP/markers.bin"
E2E_SERIAL="$E2E_TMP/serial.log"
E2E_QEMU_LOG="$E2E_TMP/qemu.log"

# Marker region is BM_STAGE_MAX (176) * 8 = 1408 bytes; round to 1536.
E2E_MARKER_REGION_SIZE=1536

e2e_log() { echo "[e2e] $*" >&2; }
e2e_fail() {
    echo "FAIL: $*" >&2
    if [ -s "$E2E_SERIAL" ]; then
        echo "---- last serial output ----" >&2
        tr -d '\r' < "$E2E_SERIAL" | tail -n 25 >&2
    fi
    exit 1
}

E2E_BOOT_MODE=uefi
E2E_IMAGE="${E2E_IMAGE:-$E2E_OS_IMG}"
# Kept for older tests that call it directly. UEFI + bin/os.img (or
# a per-test copy in E2E_IMAGE) is the only way to boot.
e2e_select_image() {
    [ -f "$E2E_IMAGE" ] || e2e_fail "missing $E2E_IMAGE (run ./build.sh)"
    [ -f "$E2E_OVMF" ]  || e2e_fail "missing OVMF firmware $E2E_OVMF (apt install ovmf)"
}

# Build the standard QEMU command line into the E2E_QEMU_ARGS array.
e2e_qemu_args() {
    e2e_select_image
    E2E_QEMU_ARGS=(
        -machine "${E2E_MACHINE:-pc}"
        -cpu Skylake-Server
        -m "${E2E_MEM:-512M}"
        -accel tcg
        -bios "$E2E_OVMF"
        -display none
        -monitor stdio
        -serial "file:$E2E_SERIAL"
        -no-reboot
    )
    if [ "${E2E_MACHINE:-pc}" = q35 ]; then
        # q35 has AHCI, not IDE: attach the image to the ICH9 AHCI port.
        E2E_QEMU_ARGS+=(-drive "file=$E2E_IMAGE,format=raw,if=none,id=os,snapshot=on"
                        -device ide-hd,drive=os,bus=ide.0)
    else
        E2E_QEMU_ARGS+=(-drive "file=$E2E_IMAGE,format=raw,if=ide,snapshot=on")
    fi
    if [ -n "${E2E_EXTRA_QEMU_ARGS:-}" ]; then
        # shellcheck disable=SC2206
        local extra=($E2E_EXTRA_QEMU_ARGS)
        E2E_QEMU_ARGS+=("${extra[@]}")
    fi
}

# e2e_run_qemu <timeout-seconds> <monitor-script-function>
# The function's stdout is fed to the QEMU monitor; it must end
# with `quit`.
e2e_run_qemu() {
    local limit=$1 fn=$2
    e2e_qemu_args
    rm -f "$E2E_SERIAL"
    "$fn" | timeout "$limit" qemu-system-x86_64 "${E2E_QEMU_ARGS[@]}" \
        > "$E2E_QEMU_LOG" 2>&1 || true
}

# e2e_wait_serial <ERE> <max-seconds>: poll the serial log until it
# matches or the time runs out. For use inside monitor scripts.
e2e_wait_serial() {
    local re=$1 limit=$2 waited=0
    while [ "$waited" -lt $((limit * 2)) ]; do
        if tr -d '\r' 2>/dev/null < "$E2E_SERIAL" | grep -Eq -- "$re"; then
            return 0
        fi
        sleep 0.5
        waited=$((waited + 1))
    done
    return 1
}

# boot_and_dump <seconds> [sentinel-ERE]
# Boot, wait until the serial log matches the sentinel (default
# E2E_SENTINEL: BLANK.ELF finished its self-test, so every boot and
# user marker is written), then dump the marker region.
#
# <seconds> is the old fixed boot budget. It now only scales the
# timeout (max(3 x seconds, 90 s)): a slow boot - several QEMUs in
# parallel under TCG - waits longer instead of failing, and a fast
# one does not sleep needlessly. A sentinel of "" restores the
# fixed sleep.
E2E_SENTINEL="${E2E_SENTINEL:-^e2e: user selftest done}"
boot_and_dump() {
    local seconds=${1:-6}
    local sentinel=${2-$E2E_SENTINEL}
    local limit=$((seconds * 3))
    [ "$limit" -ge 90 ] || limit=90
    _e2e_dump_script() {
        if [ -n "$sentinel" ]; then
            e2e_wait_serial "$sentinel" "$limit" || true
            sleep 1
        else
            sleep "$seconds"
        fi
        printf 'pmemsave 0x6000 %d "%s"\n' "$E2E_MARKER_REGION_SIZE" "$E2E_DUMP"
        printf 'quit\n'
    }

    # Cold-start absorber: the very first QEMU after the WSL distro
    # warms up sometimes loses the monitor-command race and exits
    # before pmemsave delivers. One retry is enough.
    local attempt
    for attempt in 1 2; do
        rm -f "$E2E_DUMP"
        if [ "$attempt" -gt 1 ]; then
            e2e_log "warming up (attempt $attempt)"
            sleep 1
        fi
        e2e_log "booting (machine=${E2E_MACHINE:-pc}, image=$E2E_IMAGE, wait for /${sentinel}/ up to ${limit} s)"
        e2e_run_qemu $((limit + 15)) _e2e_dump_script
        if [ -s "$E2E_DUMP" ]; then
            break
        fi
    done

    e2e_log "qemu exit, dump size=$(stat -c %s "$E2E_DUMP" 2>/dev/null || echo missing)"

    [ -s "$E2E_DUMP" ] || e2e_fail "pmemsave produced no dump ($(tail -n 3 "$E2E_QEMU_LOG" 2>/dev/null))"
}

# expect_serial <extended-regex> [name]
expect_serial() {
    local re=$1 name=${2:-serial matches /$1/}
    if tr -d '\r' 2>/dev/null < "$E2E_SERIAL" | grep -Eq -- "$re"; then
        e2e_log "$name: ok"
    else
        e2e_fail "$name: serial log has no match for /$re/"
    fi
}

# expect_no_serial <extended-regex> [name]
expect_no_serial() {
    local re=$1 name=${2:-serial lacks /$1/}
    if tr -d '\r' 2>/dev/null < "$E2E_SERIAL" | grep -Eq -- "$re"; then
        e2e_fail "$name: serial log unexpectedly matches /$re/"
    fi
    e2e_log "$name: ok"
}

# Read the 32-bit half of marker slot $1 at byte offset $2 of slot.
# Output: 8-char lowercase hex with no prefix.
_marker_word() {
    local stage=$1 byte_off=$2
    local file_off=$(( stage * 8 + byte_off ))
    od -An -v -tx4 -N 4 -j $file_off "$E2E_DUMP" | tr -d ' \n'
}

# Marker high half (bits 63..32). Magic prefix lives here.
marker_high() { _marker_word "$1" 4; }

# Marker low half (bits 31..0). Caller's value lives here.
marker_low()  { _marker_word "$1" 0; }

# The high 32 bits are the constant magic for every stage; the
# stage id is byte 3 of the LOW word.
expected_high() { echo "b007c0de"; }
expected_stage_byte() { printf '%02x' "$1"; }

# Show every marker we have so a failure isn't a binary
# "yes/no" but a trace of how far boot got.
dump_marker_summary() {
    local s hi
    for ((s=1; s<176; s++)); do
        hi=$(marker_high "$s")
        if [ "$hi" = "$(expected_high)" ]; then
            local v
            v=$(marker_value "$s")
            echo "  stage $s: REACHED (value=$v)"
        fi
    done
}

expect_stage_reached() {
    local stage=$1
    local name=${2:-stage $stage}
    local hi lo
    hi=$(marker_high "$stage")
    lo=$(marker_low  "$stage")
    if [ "$hi" != "$(expected_high)" ]; then
        echo "Marker dump after boot (reached stages only):" >&2
        dump_marker_summary >&2
        e2e_fail "$name: high half is $hi, expected $(expected_high) (stage never reached)"
    fi
    # Top byte of low word should be the stage id.
    local lo_top=${lo:0:2}
    if [ "$lo_top" != "$(expected_stage_byte $stage)" ]; then
        e2e_fail "$name: low-word top byte is $lo_top, expected $(expected_stage_byte $stage)"
    fi
    e2e_log "$name: reached (raw hi=$hi lo=$lo)"
}

# Extract the value the kernel stored (low 24 bits of low word).
marker_value() {
    local stage=$1
    local lo
    lo=$(marker_low "$stage")
    # low word format: SS VV VV VV  (8 hex chars)
    local val_hex=${lo:2}
    printf '%d' 0x"$val_hex"
}

# expect_stage_value <stage> <op> <expected> [name]
# Supported ops: -eq -ne -gt -ge -lt -le
expect_stage_value() {
    local stage=$1 op=$2 expected=$3
    local name=${4:-stage $stage value}
    expect_stage_reached "$stage" "$name"
    local got
    got=$(marker_value "$stage")
    if ! [ "$got" "$op" "$expected" ]; then
        e2e_fail "$name: got $got, expected $op $expected"
    fi
    e2e_log "$name: value $got (passed $op $expected)"
}
