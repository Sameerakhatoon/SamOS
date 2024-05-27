#!/usr/bin/env bash
# S-PAR - PCI on aarch64 -machine virt,gic-version=3 under -kernel, where
# no firmware assigned anything (the e2e PCI tests, 77, 162, 195-205,
# run on aarch64 through the hook as well):
#   bars:   an NVMe, a qemu-xhci with a USB stick and a virtio-blk-pci:
#           every BAR assigned from the host bridge's ranges (the
#           virtio's 64-bit prefetchable BAR from the window at 512 GiB,
#           the rest below 4 GiB), the NVMe disk up, the virtio disk's
#           INTx a GIC SPI from the interrupt-map, the xHCI's MSI-X LPI
#           8204 through the ITS; the stick read at the interrupt (no
#           "completion interrupt missing").
#   smp4:   the same on four CPUs: one ITS collection per redistributor.
#   noits:  virt,its=off: no ITS, the xHCI falls back to INTx (an SPI).
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
timeout_s="${ARCH_TIMEOUT:-180}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

checks=0
failures=0

bash "$root/arch-build.sh" aarch64 > "$tmp/build.log" 2>&1 || {
    cat "$tmp/build.log"
    echo "FAIL: aarch64 build"
    echo "aarch64-pci arch test: 1 checks, 1 failures"
    exit 1
}

truncate -s 4M "$tmp/nvme.img"
truncate -s 4M "$tmp/vblk.img"
mkfs.fat -F 16 -n STICK -C "$tmp/stick.img" 32768 > /dev/null
devices="-drive file=$tmp/nvme.img,if=none,id=nvm,format=raw -device nvme,drive=nvm,serial=pa
 -drive file=$tmp/vblk.img,if=none,id=vb,format=raw -device virtio-blk-pci,drive=vb
 -device qemu-xhci,id=xhci -drive file=$tmp/stick.img,if=none,id=st,format=raw
 -device usb-storage,bus=xhci.0,drive=st"

# boot <name> <machine options> <qemu args> <ERE>... : until every ERE
# matched.
boot() {
    local name="$1" machine="$2" args="$3"
    shift 3
    local log="$tmp/$name.log"
    : > "$log"
    # shellcheck disable=SC2086
    qemu-system-aarch64 -machine "virt,gic-version=3$machine" -cpu cortex-a72 -m 512M -nographic \
        -accel tcg,thread=multi -kernel "$root/bin/aarch64/kernel.img" \
        -initrd "$root/bin/aarch64/ramdisk.img" $args \
        < /dev/null > "$log" 2>&1 &
    local pid=$! waited=0
    while kill -0 "$pid" 2>/dev/null; do
        local all=1 re
        tr -d '\r' < "$log" > "$log.now"
        for re in "$@"; do
            grep -aE "$re" "$log.now" > /dev/null || { all=0; break; }
        done
        [ $all -eq 1 ] && break
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

result() {
    checks=$((checks + 1))
    if [ "$1" -eq 0 ]; then
        echo "PASS: $2"
    else
        failures=$((failures + 1))
        echo "FAIL: $2"
    fi
}

expect() {
    if grep -aE "$2" "$tmp/$1.log.txt" > /dev/null; then
        result 0 "$1: $3"
    else
        result 1 "$1: $3 (no match for /$2/)"
    fi
}

expect_no() {
    if grep -aE "$2" "$tmp/$1.log.txt" > /dev/null; then
        result 1 "$1: $3 (unexpected /$2/)"
    else
        result 0 "$1: $3"
    fi
}

no_crash() {
    expect_no "$1" '^exception:|KERNEL PANIC|^trap:|^irq: unexpected|spinlock:|USER FAULT|its: command' \
        "no exception, panic, stray IRQ, lock timeout or ITS stall"
}

done_re='^e2e: user selftest done$'
stick_re='^usb-msc: slot [0-9]+, QEMU QEMU HARDDISK, disk [0-9]+, 32 MiB$'

pci_checks() {
    local n=$1
    expect "$n" '^pci: 0:[0-9]+\.0 BAR4 mem64 pref 0x00000080000[0-9a-f]{5} size ' "a 64-bit prefetchable BAR from the 64-bit window"
    expect "$n" '^pci: 0:[0-9]+\.0 BAR0 mem32 0x000000001[0-9a-f]{7} size ' "32-bit BARs from the window at 0x10000000"
    expect "$n" '^pci: 0:[0-9]+\.0 BAR0 io 0x0000000000001[0-9a-f]{3} size ' "an I/O BAR from the I/O window"
    expect_no "$n" '^pci: no room' "every BAR fitted"
    expect "$n" '^nvme: controller ready, disk [0-9]+$' "NVMe up on its assigned BAR"
    expect "$n" '^disk: virtio-blk completions by interrupt, GSI 3[5-8] level active-high, vector 3[5-8]$' \
        "virtio-blk-pci INTx through the interrupt-map"
    expect "$n" "$stick_re" "the stick is a disk"
    expect_no "$n" 'completion interrupt missing|timed out|disk disabled' "every completion came at its interrupt"
    expect "$n" "$done_re" "BLANK.ELF's self-test"
    no_crash "$n"
}

boot bars "" "-smp 1 $devices" "$done_re"
pci_checks bars
expect bars '^its: GICv3 ITS at 0x8080000, 1 collection\(s\), LPIs 8192-8255, doorbell 0x8090040$' "the ITS, one collection"
expect bars '^xhci: interrupts by MSI-X, vector 8204$' "xHCI MSI-X as LPI 8204"

boot smp4 "" "-smp 4 $devices" "$done_re" '^smp: 4 cpus online$'
pci_checks smp4
expect smp4 '^its: GICv3 ITS at 0x8080000, 4 collection\(s\)' "one collection per redistributor"
expect smp4 '^xhci: interrupts by MSI-X, vector 8204$' "xHCI MSI-X as LPI 8204"
expect smp4 '^smp: 4 cpus online$' "four CPUs online"

boot noits ",its=off" "-smp 1 $devices" "$done_re"
pci_checks noits
expect noits '^its: none in the device tree, PCI MSI off \(INTx\)$' "no ITS reported"
expect noits '^disk: xhci completions by interrupt, GSI 3[5-8] level active-high, vector 3[5-8]$' "xHCI falls back to INTx"
expect_no noits '^xhci: interrupts by MSI' "no MSI without an ITS"

echo "aarch64-pci arch test: $checks checks, $failures failures"
[ "$failures" -eq 0 ]
