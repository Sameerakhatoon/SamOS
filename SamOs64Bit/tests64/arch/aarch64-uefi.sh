#!/usr/bin/env bash
# aarch64 UEFI boot: BOOTAA64.EFI (the top-level SamOs.c built
# for AARCH64 by efi-build.sh) under AAVMF on -machine virt,gic-version=3
# -cpu cortex-a72 -m 512M. The ESP holds the loader, KERNEL.BIN (the
# arm64 Image), INITRD.IMG (the RAM disk aarch64-user.sh passes with
# -initrd) and CMDLINE.TXT; the disk is virtio-blk-pci, which AAVMF
# boots from.
#   dt:     acpi=off, so AAVMF installs QEMU's DTB: the loader copies it,
#           fills boot_info from the UEFI memory map and enters the
#           kernel with x0 = boot_info; memory map, kernel range and
#           command line arrive; kernel_main runs HELLO.ELF with its
#           arguments and BLANK.ELF's selftest at EL0.
#   acpi:   the default acpi=on (AAVMF then gives ACPI and no DTB), -smp
#           4: the MADT GICD/GICR/GICC entries, SPCR, GTDT and FADT are
#           read and the kernel runs on the DT built from them, up to
#           the same user programs. Its GIC, UART and timer must be the
#           ones the dt boot found in QEMU's DT. With SMP, the three
#           other CPUs start from the MADT's GICCs with PSCI CPU_ON
#           over the FADT's conduit (hvc), and the SMP self-test passes.
#   el2:    virtualization=on: AAVMF runs at EL2, the loader turns the
#           EL2 MMU off and the kernel drops to EL1 as on -kernel. -smp
#           2: the second CPU starts at EL2 too, through PSCI over smc.
#   gop:    -device ramfb: AAVMF's GOP mode lands in boot_info.
# The -kernel boots stay in the other aarch64-*.sh tests.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
top="$(cd "$root/.." && pwd)"
timeout_s="${ARCH_TIMEOUT:-90}"
aavmf=/usr/share/AAVMF
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

checks=0
failures=0

for tool in sgdisk mkfs.fat mcopy mmd; do
    command -v "$tool" > /dev/null || { echo "aarch64-uefi arch test: SKIP (no $tool)"; exit 0; }
done
if [ ! -f "$aavmf/AAVMF_CODE.fd" ] || [ ! -x "${EDK2_ROOT:-$HOME/edk2}/BaseTools/Source/C/bin/GenFw" ]; then
    echo "aarch64-uefi arch test: SKIP (no AAVMF in $aavmf or no EDK2 BaseTools)"
    exit 0
fi

bash "$root/arch-build.sh" aarch64 > "$tmp/build.log" 2>&1 &&
    bash "$top/efi-build.sh" AARCH64 "$tmp/BOOTAA64.EFI" >> "$tmp/build.log" 2>&1 || {
    cat "$tmp/build.log"
    echo "FAIL: aarch64 kernel or BOOTAA64.EFI build"
    echo "aarch64-uefi arch test: 1 checks, 1 failures"
    exit 1
}

# A 64 MiB GPT disk with one FAT16 ESP, as build.sh writes bin/os.img.
export MTOOLS_SKIP_CHECK=1 MTOOLS_NO_VFAT=1
esp="$tmp/esp.img"
truncate -s 64M "$esp"
sgdisk --clear --new=1:2048:0 --typecode=1:EF00 "$esp" > /dev/null
mkfs.fat -F 16 -n ESP --offset 2048 -h 2048 "$esp" $(((64 * 2048 - 2048 - 34) / 2)) > /dev/null
part="$esp@@$((2048 * 512))"
mmd -i "$part" ::/EFI ::/EFI/BOOT
mcopy -i "$part" "$tmp/BOOTAA64.EFI" ::/EFI/BOOT/BOOTAA64.EFI
mcopy -i "$part" "$root/bin/aarch64/kernel.img" ::/KERNEL.BIN
mcopy -i "$part" "$root/bin/aarch64/ramdisk.img" ::/INITRD.IMG
printf '%s' "samos.run=HELLO.ELF one two" > "$tmp/cmdline.txt"
mcopy -i "$part" "$tmp/cmdline.txt" ::/CMDLINE.TXT

# boot <name> <machine options> <qemu args> <ERE>... : until every ERE
# matched. Each boot has its own copy of the varstore.
boot() {
    local name="$1" machine="$2" args="$3"
    shift 3
    local log="$tmp/$name.log"
    cp "$aavmf/AAVMF_VARS.fd" "$tmp/$name.vars"
    : > "$log"
    # shellcheck disable=SC2086
    qemu-system-aarch64 -machine "virt,gic-version=3$machine" -cpu cortex-a72 -m 512M -nographic \
        -drive if=pflash,format=raw,readonly=on,file="$aavmf/AAVMF_CODE.fd" \
        -drive if=pflash,format=raw,file="$tmp/$name.vars" \
        -drive if=none,file="$esp",format=raw,id=hd,snapshot=on -device virtio-blk-pci,drive=hd $args \
        < /dev/null > "$log" 2>&1 &
    local pid=$! waited=0
    while kill -0 "$pid" 2>/dev/null; do
        local all=1 re
        tr -d '\r' < "$log" | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' > "$log.now"
        for re in "$@"; do
            grep -aqE "$re" "$log.now" || { all=0; break; }
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
    tr -d '\r' < "$log" | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' > "$log.txt"
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
    if grep -aqE "$2" "$tmp/$1.log.txt"; then
        result 0 "$1: $3"
    else
        result 1 "$1: $3 (no match for /$2/)"
    fi
}

expect_no() {
    if grep -aqE "$2" "$tmp/$1.log.txt"; then
        result 1 "$1: $3 (unexpected /$2/)"
    else
        result 0 "$1: $3"
    fi
}

# field <name> <sed -E expression printing one value>
field() {
    sed -nE "$2" "$tmp/$1.log.txt" | head -1
}

dump() {
    echo "---- $1 serial ----"
    grep -av '^BdsDxe\|^UEFI firmware\|^\s*$' "$tmp/$1.log.txt"
}

no_crash() {
    expect_no "$1" '^exception:|KERNEL PANIC|^trap:|^irq: unexpected|^mmu: .*halted' "no kernel exception, panic or stray IRQ"
}

user_ran() {
    expect "$1" '^SamOS: kernel_main$' "the shared kernel_main"
    expect "$1" '^boot_info: cmdline "samos.run=HELLO.ELF one two"$' "kernel_main's boot_info has CMDLINE.TXT"
    expect "$1" '^initrd: 0x0*[0-9a-f]+, 8192 KiB, the RAM disk$' "INITRD.IMG is the RAM disk"
    expect "$1" '^hello: argv\[2\]=two$' "HELLO.ELF ran with its arguments"
    expect "$1" '^e2e: user selftest done$' "BLANK.ELF's selftest at EL0"
    no_crash "$1"
}

done_re='^e2e: user selftest done$'

# ---- DT from the firmware ----------------------------------------------
boot dt ",acpi=off" "" "$done_re"
dump dt
expect dt '^SamOs UEFI bootloader\.$' "BOOTAA64.EFI started by AAVMF"
expect dt '^Kernel: [0-9]+ bytes at 40200000, image 40200000-[0-9A-F]+$' "arm64 Image at its 2 MiB base"
expect dt '^Device tree: [0-9]+ bytes, copied to [0-9A-F]+$' "the firmware's DTB (DTB_TABLE_GUID) copied"
expect dt '^SamOS aarch64: booting \(entered at EL1, running at EL1\)$' "kernel entered with the MMU off, through UEFI"
expect dt '^uefi: boot_info at 0x[0-9a-f]+, version 2, [1-9][0-9]* memory map entries, kernel 0x40200000-0x[0-9a-f]+$' \
    "boot_info: magic, version, memory map entries, kernel range"
expect dt '^uefi: dtb 0x[1-9a-f][0-9a-f]*, acpi rsdp 0x0, initrd 0x[0-9a-f]+ \(8388608 bytes\)$' "boot_info: DTB and initrd, no RSDP"
expect dt '^uefi: cmdline "samos.run=HELLO.ELF one two"$' "boot_info: CMDLINE.TXT"
kernel_range="$(field dt 's/^uefi: boot_info.* kernel (0x[0-9a-f]+)-(0x[0-9a-f]+)$/\1-\2/p')"
expect dt "^memmap: ${kernel_range} kernel image$" "the loader's kernel range is the image the kernel reserves"
expect dt '^memmap: 0x[0-9a-f]+-0x[0-9a-f]+ boot data$' "boot_info and the E820 table reserved"
expect dt '^memmap: 0x[0-9a-f]+-0x[0-9a-f]+ dtb$' "the DTB copy reserved"
expect dt '^console: pl011 at 0x9000000 \(stdout-path\)$' "console from the DT"
user_ran dt

# ---- ACPI only ---------------------------------------------------------
boot acpi "" "-smp 4" "$done_re"
dump acpi
expect acpi '^Device tree: none \(ACPI only\)$' "AAVMF gives no DTB with ACPI"
expect acpi '^uefi: dtb 0x0, acpi rsdp 0x[1-9a-f][0-9a-f]*, initrd' "boot_info: the RSDP, no DTB"
expect acpi '^acpi: [0-9]+ tables, madt gicd 0x8000000 \(version 3\), 1 gicr range\(s\) at 0x80a0000, 4 gicc \(4 enabled\)$' \
    "MADT: GICD, GICR range, one GICC per CPU"
expect acpi '^acpi: spcr interface 3 at 0x9000000 \(mmio\), gsiv 33$' "SPCR: the PL011 console"
expect acpi '^acpi: gtdt el1 timer gsiv 30, virtual timer gsiv 27$' "GTDT: the timer PPIs"
expect acpi '^acpi: fadt arm_boot_arch 0x3 \(psci via hvc\)$' "FADT: PSCI via HVC"
expect acpi '^acpi: device tree built from the tables, [0-9]+ bytes$' "a DT from the tables"
expect acpi '^console: pl011 at 0x9000000 \(stdout-path\)$' "console from SPCR"
expect acpi '^gic: redistributor at 0x80a0000 \(frame 0 of 4, affinity 0x0\)$' "GIC from the MADT, four redistributor frames"
expect acpi '^timer: .*EL1 physical timer INTID 30 \(DT interrupts\)$' "timer INTID from the GTDT"
expect acpi '^memmap: 0x[0-9a-f]+-0x[0-9a-f]+ acpi$' "the ACPI tables reserved"
user_ran acpi
for cpu in 1 2 3; do
    expect acpi "^smp: cpu $cpu mpidr 0x$cpu online\$" "CPU $cpu from the MADT started by PSCI CPU_ON"
done
expect acpi '^smp: 4 cpus online$' "four CPUs online"
expect acpi '^smp selftest: call, tlb shootdown, lock and atomic counts ok$' "SMP self-test: IPIs, broadcast TLB flush, locks"
for what in 'gic: GICv3 distributor at' 'gic: redistributor at' 'console: pl011 at' 'timer: .*EL1 physical timer INTID'; do
    a="$(grep -aE "^$what" "$tmp/acpi.log.txt" | head -1 | sed -E 's/ \(frame.*//')"
    d="$(grep -aE "^$what" "$tmp/dt.log.txt" | head -1 | sed -E 's/ \(frame.*//')"
    [ -n "$a" ] && [ "$a" = "$d" ]
    result $? "acpi agrees with the DT boot: ${a:-nothing}"
done

# ---- AAVMF at EL2 ------------------------------------------------------
boot el2 ",acpi=off,virtualization=on" "-smp 2" "$done_re"
expect el2 '^SamOS aarch64: booting \(entered at EL2, running at EL1\)$' "entered at EL2, dropped to EL1"
expect el2 "$done_re" "user programs at EL0"
expect el2 '^smp: cpu 1 mpidr 0x1 online$' "the second CPU, entered at EL2, online at EL1"
expect el2 '^smp selftest: call, tlb shootdown, lock and atomic counts ok$' "SMP self-test at EL1 under EL2 entry"
no_crash el2

# ---- GOP ---------------------------------------------------------------
boot gop ",acpi=off" "-device ramfb" "$done_re"
expect gop '^Framebuffer [0-9A-F]+ \([0-9]+ bytes\), [0-9]+ x [0-9]+, pitch [0-9]+, format [01]$' "the loader set a GOP mode"
expect gop '^uefi: GOP framebuffer 0x[1-9a-f][0-9a-f]*, [0-9]+x[0-9]+, pitch [0-9]+, format [01] \(not used yet\)$' \
    "the GOP framebuffer reaches the kernel in boot_info"
expect gop "$done_re" "user programs at EL0"
no_crash gop

echo "aarch64-uefi arch test: $checks checks, $failures failures"
[ $failures -eq 0 ]
