#!/usr/bin/env bash
# aarch64 boots to a serial hello under QEMU's direct -kernel.
#
# Builds bin/aarch64/kernel.img (arch-build.sh, about a second) and
# boots it three times on -machine virt -cpu cortex-a72 -m 512M:
#   el1:   plain virt, entered at EL1. Banner, PL011 found through
#          /chosen/stdout-path, 512 MiB from /memory, the bootargs
#          from -append, and the hello line.
#   el2:   virt,virtualization=on, entered at EL2: proves the drop
#          to EL1 ("entered at EL2, running at EL1"), then hello.
#   fault: samos.fault_test, brk #0x5a: the early vector table dumps
#          ESR 0xf200005a (EC 0x3c, BRK) with ELR inside the kernel.
# The kernel halts in wfi, so each boot is killed as soon as its last
# expected line is on serial (or after ARCH_TIMEOUT seconds).
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
timeout_s="${ARCH_TIMEOUT:-15}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

checks=0
failures=0

bash "$root/arch-build.sh" aarch64 > "$tmp/build.log" 2>&1 || {
    cat "$tmp/build.log"
    echo "FAIL: aarch64 build"
    echo "aarch64 arch test: 1 checks, 1 failures"
    exit 1
}

# boot <name> <machine> <append> <last line ERE>
boot() {
    local name="$1" machine="$2" append="$3" done_re="$4"
    local log="$tmp/$name.log"
    qemu-system-aarch64 -machine "$machine" -cpu cortex-a72 -m 512M -nographic \
        -kernel "$root/bin/aarch64/kernel.img" -append "$append" \
        < /dev/null > "$log" 2>&1 &
    local pid=$! waited=0
    while kill -0 "$pid" 2>/dev/null; do
        if grep -aqE "$done_re" "$log"; then
            break
        fi
        if [ $waited -ge $((timeout_s * 10)) ]; then
            echo "  ($name: timed out after ${timeout_s}s)"
            break
        fi
        sleep 0.1
        waited=$((waited + 1))
    done
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    tr -d '\r' < "$log" > "$log.txt"
}

# expect <name> <ERE> <description>
expect() {
    checks=$((checks + 1))
    if grep -aqE "$2" "$tmp/$1.log.txt"; then
        echo "PASS: $1: $3"
    else
        failures=$((failures + 1))
        echo "FAIL: $1: $3 (no match for /$2/)"
    fi
}

expect_no() {
    checks=$((checks + 1))
    if grep -aqE "$2" "$tmp/$1.log.txt"; then
        failures=$((failures + 1))
        echo "FAIL: $1: $3 (unexpected /$2/)"
    else
        echo "PASS: $1: $3"
    fi
}

dump() {
    echo "---- $1 serial ----"
    cat "$tmp/$1.log.txt"
}

boot el1 virt "samos.test=1" '^SamOS aarch64: hello'
dump el1
expect el1 '^SamOS aarch64: booting \(entered at EL1, running at EL1\)$' "banner, EL1 entry"
expect el1 '^console: pl011 at 0x9000000 \(stdout-path\)$' "PL011 from /chosen/stdout-path"
expect el1 '^fdt: 0x[0-9a-f]+, version 17, [0-9]+ bytes$' "device tree from x0"
expect el1 '^memory: 0x40000000 size 0x20000000$' "/memory reg"
expect el1 '^memory: 512 MiB in 1 region\(s\)$' "memory size"
expect el1 '^bootargs: samos.test=1$' "bootargs"
expect el1 '^SamOS aarch64: hello$' "hello"
expect_no el1 '^exception:' "no exception"

boot el2 virt,virtualization=on "samos.test=1" '^SamOS aarch64: hello'
dump el2
expect el2 '^SamOS aarch64: booting \(entered at EL2, running at EL1\)$' "EL2 entry dropped to EL1"
expect el2 '^memory: 512 MiB in 1 region\(s\)$' "memory size"
expect el2 '^bootargs: samos.test=1$' "bootargs"
expect el2 '^SamOS aarch64: hello$' "hello"
expect_no el2 '^exception:' "no exception"

boot fault virt "samos.test=1 samos.fault_test" '^exception: halted'
dump fault
expect fault '^bootargs: samos.test=1 samos.fault_test$' "bootargs"
expect fault '^exception: sync \(current EL, SP_ELx\)$' "sync exception at EL1h"
expect fault '^  ESR 0xf200005a \(EC 0x3c\)$' "ESR is BRK #0x5a"
expect fault '^  ELR 0x402[0-9a-f]{5}$' "ELR inside the kernel"
expect_no fault '^SamOS aarch64: hello$' "no hello after the fault"

echo "aarch64 arch test: $checks checks, $failures failures"
[ $failures -eq 0 ]
