#!/usr/bin/env bash
# Shared library for end-to-end runtime tests.
#
# Each e2e test boots SamOs in QEMU under OVMF (UEFI) or the
# raw BIOS disk image, lets the kernel run for a few seconds,
# then uses pmemsave to dump the bootmarker region. The library
# below provides:
#
#   boot_and_dump <seconds>     - boot QEMU, dump 0x6000 markers
#   marker_high <stage>         - high 32 bits of marker[stage]
#   marker_low  <stage>         - low 32 bits of marker[stage]
#   expect_stage_reached <stage> [name]
#                               - assert magic prefix matches
#   expect_stage_value  <stage> <op> <expected> [name]
#                               - assert low-half passes the op
#
# Magic layout (see src/bootmarker.h):
#   bits 63..56 : 0xB0       \
#   bits 55..48 : 0x07        |
#   bits 47..40 : 0xC0        > BOOT_MARKER_MAGIC_PREFIX
#   bits 39..32 : 0xDE       /
#   bits 31..24 : stage id
#   bits 23..0  : reserved (always 0)
#   bits 31..0  : value (overlaps with the reserved zeros)
#
# Bash's printf cannot do 64-bit hex, so we work with the
# marker as two 32-bit halves via `od -An -v -tx4`.

set -uo pipefail

E2E_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
E2E_OS_BIN="$E2E_ROOT/bin/os.bin"
E2E_OS_IMG="$(cd "$E2E_ROOT/.." 2>/dev/null && pwd)/bin/os.img"
E2E_OVMF="/usr/share/ovmf/OVMF.fd"

# Where the QEMU monitor will dump the marker region.
E2E_DUMP="${E2E_DUMP:-/tmp/samos-e2e-markers.bin}"

# Marker region is BM_STAGE_MAX (176) * 8 = 1408 bytes; round to 1536.
E2E_MARKER_REGION_SIZE=1536

e2e_log() { echo "[e2e] $*" >&2; }
e2e_fail() { echo "FAIL: $*" >&2; exit 1; }

# Pick the image to boot. Prefer UEFI (os.img) when available,
# fall back to BIOS (os.bin). Sets E2E_BOOT_MODE and E2E_IMAGE.
e2e_select_image() {
    if [ -f "$E2E_OS_IMG" ] && [ -f "$E2E_OVMF" ]; then
        E2E_BOOT_MODE=uefi
        E2E_IMAGE="$E2E_OS_IMG"
    else
        E2E_BOOT_MODE=bios
        E2E_IMAGE="$E2E_OS_BIN"
    fi
}
# Pre-populate so `set -u` is happy even before boot_and_dump runs.
E2E_BOOT_MODE=""
E2E_IMAGE=""

# Run QEMU for $1 seconds and capture the marker region.
boot_and_dump() {
    local seconds=${1:-6}
    e2e_select_image

    rm -f "$E2E_DUMP"

    local qemu_args=(
        -m 512M
        -accel tcg
        -display none
        -monitor stdio
        -no-reboot
        -drive "file=$E2E_IMAGE,format=raw,if=ide"
    )
    if [ "$E2E_BOOT_MODE" = "uefi" ]; then
        qemu_args+=(-bios "$E2E_OVMF" -machine pc -cpu Skylake-Server)
    fi

    # Optional extra QEMU args per test (e.g. -device nvme,...).
    # The test exports E2E_EXTRA_QEMU_ARGS as a single string;
    # we split on whitespace and append.
    if [ -n "${E2E_EXTRA_QEMU_ARGS:-}" ]; then
        # shellcheck disable=SC2206
        local extra=($E2E_EXTRA_QEMU_ARGS)
        qemu_args+=("${extra[@]}")
    fi

    # Cold-start absorber. Without this, the very first QEMU
    # invocation after the WSL distro warms up (fresh /tmp,
    # loop device cleanup from build.sh, OVMF first-page faults
    # not yet primed) sometimes loses the monitor-command race
    # and exits before pmemsave delivers. One short retry is
    # enough; the second-and-subsequent QEMU invocations within
    # the same shell never need it.
    local attempt
    for attempt in 1 2; do
        rm -f "$E2E_DUMP"
        if [ "$attempt" -gt 1 ]; then
            e2e_log "warming up (attempt $attempt)"
            sleep 1
        fi
        e2e_log "booting (mode=$E2E_BOOT_MODE, image=$E2E_IMAGE, sleep=$seconds s)"
        (
            sleep "$seconds"
            printf 'pmemsave 0x6000 %d "%s"\n' "$E2E_MARKER_REGION_SIZE" "$E2E_DUMP"
            printf 'quit\n'
        ) | timeout $((seconds + 10)) qemu-system-x86_64 "${qemu_args[@]}" \
                > /tmp/samos-e2e-qemu.log 2>&1 || true

        # Successful run produced a non-empty dump file. If so,
        # we are done; otherwise fall through to one retry.
        if [ -s "$E2E_DUMP" ]; then
            break
        fi
    done

    e2e_log "qemu exit, dump size=$(stat -c %s "$E2E_DUMP" 2>/dev/null || echo missing)"

    [ -s "$E2E_DUMP" ] || e2e_fail "pmemsave produced no dump (boot mode=$E2E_BOOT_MODE)"
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

# Expected high half for stage $1 (matches bootmarker.h
# `boot_marker_expected_high`).
expected_high_for_stage() {
    local stage=$1
    # 0xB007C0DE | (stage << 24) -> e.g. stage=5 -> 0xB007C0DE | 0x05000000
    # Lower magic byte is masked by stage. We compute via printf.
    printf 'b007c0%02x' "$stage"
    # Wait - layout is (PREFIX << 32) | (stage << 24), so the high 32
    # bits are exactly PREFIX. Stage occupies bits 31..24 of the FULL
    # 64-bit value, which is byte 3 of the LOW word, NOT the high word.
    # So expected high = "b007c0de" for every stage, and expected
    # low-byte-3 = stage. We re-issue below.
}

# Real expected high: constant magic for all stages.
expected_high() { echo "b007c0de"; }

# Real expected stage encoding lives in byte 3 of the LOW word.
# E.g. stage=5, value=0 -> low word = 0x05000000.
# E.g. stage=5, value=N -> low word = 0x05000000 | (N & 0xFFFFFF).
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
        else
            echo "  stage $s: not reached"
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
        echo "Marker dump after boot:" >&2
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
    # Strip the top byte (stage id) -> VV VV VV (6 hex chars).
    local val_hex=${lo:2}
    # printf converts hex to decimal (POSIX).
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
