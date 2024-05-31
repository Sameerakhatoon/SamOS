#!/usr/bin/env bash
# tests64/arch/riscv64-pci.sh - parity: PCI on riscv64 -machine virt
# under OpenSBI and -kernel, where nothing assigned the BARs (the e2e
# PCI tests, 77, 162, 195-205, run on riscv64 through the hook as well).
# An NVMe, a virtio-blk-pci and a qemu-xhci with a USB stick, and a
# virtio-blk on virtio-mmio:
#   plic:   the PCIe host bridge from the device tree (ECAM), every BAR
#           assigned from its ranges (the virtio's 64-bit prefetchable
#           BAR from the window at 16 GiB, the rest below 4 GiB), the
#           NVMe disk up, INTx of the virtio disk and of the xHCI
#           (no MSI without an IMSIC) through the interrupt-map to PLIC
#           sources 32-35; the stick read at the interrupt.
#   aia:    virt,aia=aplic-imsic: no PLIC; the wired lines (UART,
#           virtio-mmio, PCI INTx) through the APLIC in MSI mode, the
#           xHCI's MSI-X straight to an IMSIC interrupt file (EIID 140).
# Each on one hart and on four (per-hart PLIC contexts, per-hart IMSIC
# files), in Sv48, and on four in Sv39.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
kernel="${RISCV64_KERNEL:-$root/bin/riscv64/kernel.img}"
initrd="${RISCV64_INITRD:-$root/bin/riscv64/initrd.img}"
for f in "$kernel" "$initrd"; do
    [ -f "$f" ] || { echo "FAIL: $f missing; run bash arch-build.sh riscv64" >&2; exit 2; }
done
timeout_s="${ARCH_TIMEOUT:-240}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
checks=0
fails=0

pass() { checks=$((checks + 1)); echo "  ok: $1"; }
fail() { checks=$((checks + 1)); echo "  FAIL: $1"; fails=$((fails + 1)); }

truncate -s 4M "$tmp/nvme.img"
truncate -s 4M "$tmp/vblk.img"
truncate -s 4M "$tmp/mmio.img"
mkfs.fat -F 16 -n STICK -C "$tmp/stick.img" 32768 > /dev/null
devices="-drive file=$tmp/nvme.img,if=none,id=nvm,format=raw -device nvme,drive=nvm,serial=pr
 -drive file=$tmp/vblk.img,if=none,id=vb,format=raw -device virtio-blk-pci,drive=vb
 -device qemu-xhci,id=xhci -drive file=$tmp/stick.img,if=none,id=st,format=raw
 -device usb-storage,bus=xhci.0,drive=st
 -global virtio-mmio.force-legacy=false -drive file=$tmp/mmio.img,if=none,id=mb,format=raw
 -device virtio-blk-device,drive=mb"

done_re='^e2e: user selftest done$'

# boot <name> <machine options> <cpu> <harts>: until BLANK.ELF's
# self-test is done (and every hart is online).
boot() {
    local name=$1 machine=$2 cpu=$3 harts=$4 log="$tmp/$1.log"
    echo "riscv64 $name"
    # shellcheck disable=SC2086
    timeout "$timeout_s" qemu-system-riscv64 -machine "virt$machine" -cpu "$cpu" -smp "$harts" \
        -accel tcg,thread=multi -m 512M -bios default -kernel "$kernel" -initrd "$initrd" \
        -append "samos.test=1" $devices \
        -display none -monitor none -serial "file:$log.raw" < /dev/null > "$tmp/$name-qemu.log" 2>&1 &
    local pid=$! waited=0
    while kill -0 "$pid" 2>/dev/null && [ $waited -lt $((timeout_s * 10)) ]; do
        if [ -f "$log.raw" ] && tr -d '\r' < "$log.raw" | grep -aE "$done_re" > /dev/null &&
           { [ "$harts" = 1 ] || tr -d '\r' < "$log.raw" | grep -aE "^smp: $harts cpus online$" > /dev/null; }; then
            break
        fi
        sleep 0.1
        waited=$((waited + 1))
    done
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    tr -d '\r' < "$log.raw" > "$log"
}

expect() {
    if grep -aE "$2" "$tmp/$1.log" > /dev/null; then pass "$1: $3"; else fail "$1: $3 (no match for /$2/)"; fi
}

expect_no() {
    if grep -aE "$2" "$tmp/$1.log" > /dev/null; then fail "$1: $3 (unexpected /$2/)"; else pass "$1: $3"; fi
}

pci_checks() {
    local n=$1
    expect "$n" '^pci: config access ECAM, base 0x0000000030000000, buses 0-255$' "the host bridge's ECAM window"
    expect "$n" '^pci: 0:[0-9]+\.0 BAR4 mem64 pref 0x00000004000[0-9a-f]{5} size ' "a 64-bit prefetchable BAR from the 64-bit window"
    expect "$n" '^pci: 0:[0-9]+\.0 BAR0 mem32 0x000000004[0-9a-f]{7} size ' "32-bit BARs from the window at 0x40000000"
    expect "$n" '^pci: 0:[0-9]+\.0 BAR0 io 0x0000000000001[0-9a-f]{3} size ' "an I/O BAR from the I/O window"
    expect_no "$n" '^pci: no room' "every BAR fitted"
    expect "$n" '^nvme: controller ready, disk [0-9]+$' "NVMe up on its assigned BAR"
    expect "$n" '^disk: virtio-blk completions by interrupt, GSI 3[2-5] level active-high, vector 3[2-5]$' \
        "virtio-blk-pci INTx through the interrupt-map"
    expect "$n" '^usb-msc: slot [0-9]+, QEMU QEMU HARDDISK, disk [0-9]+, 32 MiB$' "the stick is a disk"
    expect_no "$n" 'completion interrupt missing|timed out|disk disabled|has no handler' \
        "every completion came at its interrupt"
    expect "$n" "$done_re" "BLANK.ELF's self-test"
    expect_no "$n" 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' "no fault"
}

plic_checks() {
    local n=$1
    expect "$n" '^plic: 0xc000000, 95 sources, context [0-9]+$' "the PLIC"
    expect "$n" '^disk: xhci completions by interrupt, GSI 3[2-5] level active-high, vector 3[2-5]$' \
        "xHCI INTx (no IMSIC, no MSI) through the interrupt-map"
    expect_no "$n" '^xhci: interrupts by MSI' "no MSI without an IMSIC"
}

aia_checks() {
    local n=$1
    expect "$n" '^aplic: 0xd000000, 96 sources, msi mode$' "the APLIC in MSI mode"
    expect "$n" '^imsic: 0x28000000, 255 ids, hart index [0-3]$' "the S-level IMSIC"
    expect "$n" '^uart: keys from aplic irq 10$' "the UART's line through the APLIC"
    expect "$n" '^disk: virtio-blk completions by interrupt, GSI 8 level active-high, vector 8$' \
        "the virtio-mmio disk through the APLIC"
    expect "$n" '^xhci: interrupts by MSI-X, vector 140$' "xHCI MSI-X as IMSIC EIID 140"
}

for mode in sv48 sv39; do
    if [ $mode = sv48 ]; then cpu=rv64; else cpu=rv64,sv48=off; fi
    for harts in 1 4; do
        [ $mode = sv39 ] && [ $harts = 1 ] && continue
        for chip in plic aia; do
            name="$chip-$mode-smp$harts"
            opts=""
            [ $chip = aia ] && opts=",aia=aplic-imsic"
            boot "$name" "$opts" "$cpu" "$harts"
            expect "$name" "^mmu: $mode, " "paging in $mode"
            [ "$harts" = 4 ] && expect "$name" '^smp: 4 cpus online$' "four harts online"
            pci_checks "$name"
            ${chip}_checks "$name"
        done
    done
done

echo "riscv64 arch test: $checks checks, $fails failures"
if [ $fails -eq 0 ]; then
    echo "PASS: tests64/arch/riscv64-pci"
else
    echo "FAIL: tests64/arch/riscv64-pci ($fails failed)"
    exit 1
fi
