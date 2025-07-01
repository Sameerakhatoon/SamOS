#!/usr/bin/env bash
# Shared library for end-to-end runtime tests.
#
# Each e2e test boots SamOS in QEMU under OVMF (UEFI), lets the
# kernel run for a few seconds, then uses pmemsave to dump the
# bootmarker region. Everything the kernel prints also goes to COM1,
# which QEMU writes to a per-test serial log. The library provides:
#
#   boot_and_dump <seconds>     - boot QEMU, dump the markers, keep serial log
#   e2e_qemu_args               - fill the E2E_QEMU_ARGS array (for custom runs)
#   e2e_run_qemu <timeout> <fn> - run QEMU, piping <fn>'s output to the monitor
#   e2e_pmemsave [file]         - (monitor script) the pmemsave line for the markers
#   e2e_arch_has <feature>      - the arch provides it (E2E_ARCH_PROVIDES)
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
# (default 512M), E2E_SMP (CPUs, default 1; above 1 QEMU runs one
# host thread per vCPU, -accel tcg,thread=multi), E2E_EXTRA_QEMU_ARGS,
# E2E_BOOT_BUS=virtio (boot disk on virtio-blk-pci), E2E_KEEP=1 to keep
# E2E_TMP.
#
# E2E_ARCH (x86_64 by default, aarch64) picks the machine. All
# that differs per architecture is in one hook file, _arch_<arch>.sh,
# sourced below with E2E_ROOT set. It defines:
#
#   E2E_OS_IMG           the boot disk image: GPT, partition 1 at 1 MiB
#                        (FAT16, holds CMDLINE.TXT), partition 2 the
#                        SAMOS volume ("@:/") with the user programs
#   E2E_QEMU_BIN         qemu-system-<arch>
#   E2E_ARCH_PROVIDES    feature words; run-all.sh skips a test whose
#                        "# e2e-requires:" line names one not listed
#   e2e_arch_build       build the kernel, programs and E2E_OS_IMG
#                        (tests64/run-all.sh runs it once before a
#                        non-default arch's suite)
#   e2e_arch_check       e2e_fail unless the image (E2E_IMAGE) and
#                        whatever else QEMU needs are there
#   e2e_arch_qemu_args   append the machine, CPU, firmware or kernel,
#                        command line and the boot disk (E2E_IMAGE,
#                        snapshot=on) to E2E_QEMU_ARGS; the memory,
#                        CPUs, monitor, serial and the rest are added
#                        here
#   e2e_arch_marker_addr print the physical address of the boot marker
#                        region (it may read E2E_SERIAL: it is called
#                        once the guest has booted)
#
# Tests drive the guest through the QEMU monitor (sendkey, pmemsave,
# screendump) and read the first serial port back, on every arch.
#
# Tests run with pipefail: a reader that stops early (grep -q, grep -m,
# head, awk exit) after a pipe can kill the writer with SIGPIPE, and
# the pipeline then fails although the match is there. Use
# `grep ... > /dev/null` and readers that read to the end.
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
E2E_ARCH="${E2E_ARCH:-x86_64}"
if [ ! -f "$E2E_ROOT/tests64/e2e/_arch_$E2E_ARCH.sh" ]; then
    echo "FAIL: no e2e hook for E2E_ARCH=$E2E_ARCH (tests64/e2e/_arch_$E2E_ARCH.sh)" >&2
    exit 1
fi
. "$E2E_ROOT/tests64/e2e/_arch_$E2E_ARCH.sh"

E2E_TMP="$(mktemp -d "${TMPDIR:-/tmp}/samos-e2e.XXXXXX")"
if [ "${E2E_KEEP:-0}" != 1 ]; then
    trap 'rm -rf "$E2E_TMP"' EXIT
fi
E2E_DUMP="$E2E_TMP/markers.bin"
E2E_SERIAL="$E2E_TMP/serial.log"
E2E_QEMU_LOG="$E2E_TMP/qemu.log"

# Marker region is BM_STAGE_MAX (196) * 8 = 1568 bytes; round to 1600.
E2E_MARKER_REGION_SIZE=1600

e2e_log() { echo "[e2e] $*" >&2; }
e2e_fail() {
    echo "FAIL: $*" >&2
    if [ -s "$E2E_SERIAL" ]; then
        echo "---- last serial output ----" >&2
        tr -d '\r' < "$E2E_SERIAL" | tail -n 25 >&2
    fi
    exit 1
}
# A tool the test needs is missing (not the kernel's fault):
# exit 77, which run-all.sh counts as skipped, with this reason.
e2e_skip() {
    echo "SKIP: $*"
    exit 77
}

E2E_BOOT_MODE=uefi
E2E_IMAGE="${E2E_IMAGE:-$E2E_OS_IMG}"
# Kept for older tests that call it directly: the arch hook checks the
# image (bin/os.img or a per-test copy in E2E_IMAGE) and its firmware.
e2e_select_image() {
    e2e_arch_check
}

# Build the standard QEMU command line into the E2E_QEMU_ARGS array.
e2e_qemu_args() {
    e2e_select_image
    local smp=${E2E_SMP:-1} accel=tcg
    [ "$smp" -gt 1 ] && accel=tcg,thread=multi
    E2E_QEMU_ARGS=()
    e2e_arch_qemu_args
    E2E_QEMU_ARGS+=(
        -m "${E2E_MEM:-512M}"
        -smp "$smp"
        -accel "$accel"
        -display none
        -monitor stdio
        -serial "file:$E2E_SERIAL"
        -no-reboot
    )
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
    "$fn" | timeout "$limit" "$E2E_QEMU_BIN" "${E2E_QEMU_ARGS[@]}" \
        > "$E2E_QEMU_LOG" 2>&1 || true
}

# e2e_net_args <device>: the hooks' NIC. E2E_NET=pci or mmio
# picks virtio-net-pci or virtio-net-device (the hook passes the one its
# machine has); its backend is E2E_NETDEV, a whole -netdev with id net0
# (default QEMU's user network, 10.0.2.0/24 with the gateway 10.0.2.2,
# plus E2E_NET_OPTS, e.g. ",hostfwd=tcp::8080-:80"). E2E_NET_MAC
# sets the NIC's MAC (QEMU's default is 52:54:00:12:34:56), for two
# guests on one link (e2e/229). E2E_NETDEV2, a whole -netdev
# with id net1, adds a second NIC of the same kind on it (the kernel's
# eth1), with the MAC E2E_NET_MAC2 if set (e2e/232, a router).
e2e_net_args() {
    E2E_QEMU_ARGS+=(-netdev "${E2E_NETDEV:-user,id=net0${E2E_NET_OPTS:-}}"
                    -device "$1,netdev=net0${E2E_NET_MAC:+,mac=$E2E_NET_MAC}")
    if [ -n "${E2E_NETDEV2:-}" ]; then
        E2E_QEMU_ARGS+=(-netdev "$E2E_NETDEV2" -device "$1,netdev=net1${E2E_NET_MAC2:+,mac=$E2E_NET_MAC2}")
    fi
}

# e2e_arch_has <feature>: the arch hook lists it in E2E_ARCH_PROVIDES,
# for a check inside a test that only one machine can make.
e2e_arch_has() {
    case " $E2E_ARCH_PROVIDES " in *" $1 "*) return 0 ;; esac
    return 1
}

# e2e_pmemsave [file]: in a monitor script, the command that dumps the
# marker region to <file> (default E2E_DUMP).
e2e_pmemsave() {
    printf 'pmemsave %s %d "%s"\n' "$(e2e_arch_marker_addr)" "$E2E_MARKER_REGION_SIZE" "${1:-$E2E_DUMP}"
}

# e2e_wait_serial <ERE> <max-seconds>: poll the serial log until it
# matches or the time runs out. For use inside monitor scripts.
e2e_wait_serial() {
    local re=$1 limit=$2 waited=0
    while [ "$waited" -lt $((limit * 2)) ]; do
        if tr -d '\r' 2>/dev/null < "$E2E_SERIAL" | grep -E -- "$re" > /dev/null; then
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
#
# E2E_ALSO_WAIT (ERE, optional): wait for this line as well. Since
# User processes run concurrently, so a test that starts its own
# program next to BLANK.ELF waits for both to finish.
E2E_SENTINEL="${E2E_SENTINEL:-^e2e: user selftest done}"
boot_and_dump() {
    local seconds=${1:-6}
    local sentinel=${2-$E2E_SENTINEL}
    local limit=$((seconds * 3))
    [ "$limit" -ge 90 ] || limit=90
    _e2e_dump_script() {
        if [ -n "$sentinel" ]; then
            e2e_wait_serial "$sentinel" "$limit" || true
            if [ -n "${E2E_ALSO_WAIT:-}" ]; then
                e2e_wait_serial "$E2E_ALSO_WAIT" "$limit" || true
            fi
            sleep 1
        else
            sleep "$seconds"
        fi
        e2e_pmemsave
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
# Grep reads all its input (no -q): with pipefail, a grep that
# quits at the first match can leave tr writing into a closed pipe, and
# the pipeline then fails with SIGPIPE although the line is there (a
# long log, a loaded host). expect_no_serial would pass on such a match.
expect_serial() {
    local re=$1 name=${2:-serial matches /$1/}
    if tr -d '\r' 2>/dev/null < "$E2E_SERIAL" | grep -E -- "$re" > /dev/null; then
        e2e_log "$name: ok"
    else
        e2e_fail "$name: serial log has no match for /$re/"
    fi
}

# expect_no_serial <extended-regex> [name]
expect_no_serial() {
    local re=$1 name=${2:-serial lacks /$1/}
    if tr -d '\r' 2>/dev/null < "$E2E_SERIAL" | grep -E -- "$re" > /dev/null; then
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
    for ((s=1; s<196; s++)); do
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
