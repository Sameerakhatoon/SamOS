#!/usr/bin/env bash
# The Raspberry Pi work that QEMU can check (the board itself needs
# real hardware):
#
#   image:  make ARCH=aarch64 pi: kernel8.img is kernel.img (an arm64
#           Image header), config.txt has the settings the firmware needs,
#           sdcard.img is MBR with one FAT32 partition labelled SAMOS that
#           holds both and the programs.
#   pi3:    QEMU raspi3b (BCM2837, 4 x Cortex-A53, no GIC) with
#           dtb-raspi3b.py's device tree in x0 and sdcard.img as its SD
#           card (256 MiB: a byte-addressed SDSC card), -kernel at 0x200000
#           as the Pi firmware loads kernel8.img: the console, memory and
#           devices from the device tree; the BCM2836 per-core controller
#           (timer, mailbox IPIs) and the ARM controller (the PL011's
#           receive line); the SD card on the Arasan SDHCI by PIO, its
#           FAT32 partition the kernel's "@:/"; the mailbox framebuffer;
#           the three other cores from their spin tables, the SMP
#           self-test. Then the shell over serial writes a file to the SD
#           card, "reboot" restarts through the PM watchdog (the same QEMU,
#           the same card), and the file reads back; the screen (a
#           screendump of the mailbox framebuffer) shows the shell; mtools
#           reads the file on the host.
#   sdhc:   the same card grown to 4 GiB, which QEMU makes an SDHC card
#           (block addressing): boot, write, read back on the host.
#   v2smp:  QEMU virt,gic-version=2 -smp 4 (the GICv2 path the Pi 4 and 5's
#           GIC-400 takes): the CPUs by PSCI, the SMP self-test (SGIs by
#           target list), KTEST cpubusy: every CPU's own timer ticks.
#   v2el2:  the same at EL2 (virtualization=on, PSCI over smc), -smp 2.
#   v2shell: GICv2 -smp 4 with two virtio-blk disks and ramfb: the
#           completions and the PL011's bytes come by interrupt (SPIs
#           through GICD_ITARGETSR), shell commands on both disks.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
timeout_s="${ARCH_TIMEOUT:-240}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
export MTOOLS_SKIP_CHECK=1

checks=0
failures=0
result() {
    checks=$((checks + 1))
    if [ "$1" -eq 0 ]; then echo "PASS: $2"; else failures=$((failures + 1)); echo "FAIL: $2"; fi
}

{ bash "$root/arch-build.sh" aarch64 && make -C "$root" ARCH=aarch64 pi; } > "$tmp/build.log" 2>&1 || {
    tail -n 30 "$tmp/build.log"
    echo "FAIL: aarch64 build, make pi"
    echo "aarch64-pi arch test: 1 checks, 1 failures"
    exit 1
}
pi="$root/bin/aarch64/pi"
kernel="$root/bin/aarch64/kernel.img"

# ---- image ------------------------------------------------------------
cmp -s "$kernel" "$pi/kernel8.img"
result $? "image: kernel8.img is kernel.img"
[ "$(od -An -tx1 -j56 -N4 "$pi/kernel8.img" | tr -d ' \n')" = 41524d64 ] &&
    [ "$(od -An -tu8 -j8 -N8 "$pi/kernel8.img" | tr -d ' ')" = 0 ] &&
    [ "$(od -An -tu8 -j16 -N8 "$pi/kernel8.img" | tr -d ' ')" -gt "$(stat -c %s "$pi/kernel8.img")" ]
result $? "image: arm64 Image header (ARM\\x64, text_offset 0, image_size covers .bss)"
for kv in arm_64bit=1 kernel=kernel8.img kernel_address=0x200000 enable_uart=1 dtoverlay=disable-bt enable_gic=1; do
    grep -qx "$kv" "$pi/config.txt"
    result $? "image: config.txt $kv"
done
# (whole outputs to files first: no grep -q on a pipe under pipefail)
sfdisk -d "$pi/sdcard.img" > "$tmp/sfdisk" 2> /dev/null
grep -qE '^[^ ]+1 : start= +2048, size= +522240, type=c$' "$tmp/sfdisk"
result $? "image: MBR, partition 1 FAT32 LBA (0x0c) from sector 2048"
minfo -i "$pi/sdcard.img@@1M" > "$tmp/minfo" 2> /dev/null
grep -q 'disk label="SAMOS      "' "$tmp/minfo" && grep -q 'disk type="FAT32   "' "$tmp/minfo"
result $? "image: FAT32, label SAMOS"
mdir -b -i "$pi/sdcard.img@@1M" ::/ > "$tmp/files" 2> /dev/null
for f in KERNEL8.IMG CONFIG.TXT BLANK.ELF SHELL.ELF HELLO.ELF SYSFONT.BMP RAMDISK.IMG; do
    grep -q "^::/$f\$" "$tmp/files"
    result $? "image: $f on the card"
done
mcopy -i "$pi/sdcard.img@@1M" ::/KERNEL8.IMG "$tmp/k8" 2> /dev/null && cmp -s "$tmp/k8" "$kernel"
result $? "image: the card's KERNEL8.IMG is the kernel"

# ---- helpers ----------------------------------------------------------
# count <log> <fixed string>: lines of the log that contain it
count() {
    tr -d '\r' < "$1" 2> /dev/null | grep -acF -- "$2"
}

# wait_n <log> <fixed string> <n> <seconds>
wait_n() {
    local t=0
    while [ $t -lt $(($4 * 10)) ]; do
        [ "$(count "$1" "$2")" -ge "$3" ] && return 0
        sleep 0.1
        t=$((t + 1))
    done
    echo "  (timed out waiting for '$2' x$3)" >&2
    return 1
}

# cmd <log> <line>: type it, Enter as CR, wait for the next prompt.
cmd() {
    local from t=0
    from=$(stat -c %s "$1" 2> /dev/null || echo 0)
    printf '%s\r' "$2"
    while [ $t -lt 600 ]; do
        tail -c +"$((from + 1))" "$1" | tr -d '\r' | sed '1d' | grep -a '^> ' > /dev/null && return 0
        sleep 0.1
        t=$((t + 1))
    done
    echo "  (no prompt after '$2')" >&2
}

# to_shell <log> <boot number>: BLANK.ELF's self-test of that boot,
# then the shell's banner.
to_shell() {
    wait_n "$1" 'e2e: user selftest done' "$2" "$timeout_s" || return 1
    sleep 1
    printf '\r'
    wait_n "$1" 'SamOs v1.0.0 - type help' "$2" 60 || return 1
    sleep 1
}

expect() {
    grep -aqE -- "$2" "$tmp/$1.txt"
    result $? "$1: $3"
}

expect_no() {
    grep -aqE -- "$2" "$tmp/$1.txt"
    [ $? -ne 0 ]
    result $? "$1: $3"
}

no_crash() {
    expect_no "$1" 'KERNEL PANIC|KERNEL FAULT|USER FAULT|^exception:|^trap:|^irq: unexpected|spinlock:|request timed out|completion interrupt missing|^sdhci: .*(failed|did not)' \
        "no fault, panic, stray IRQ, SD error or polling fallback"
}

raspi3b=(qemu-system-aarch64 -machine raspi3b -nographic -kernel "$kernel" -dtb "$tmp/raspi3b.dtb")

python3 -I "$here/dtb-raspi3b.py" "$tmp/raspi3b.dtb" ""
result $? "dtb-raspi3b.py: a device tree for raspi3b"

# ---- pi3: raspi3b, SD card, framebuffer, spin tables, reboot ------------
cp "$pi/sdcard.img" "$tmp/pi3.img"
# The Pi firmware's files have long names (VFAT entries next to the 8.3
# ones): dummies of them on the card, as a real card will have.
printf 'dtb\n' > "$tmp/bcm2711-rpi-4-b.dtb"
printf 'overlay\n' > "$tmp/disable-bt.dtbo"
mcopy -i "$tmp/pi3.img@@1M" "$tmp/bcm2711-rpi-4-b.dtb" ::/ && mmd -i "$tmp/pi3.img@@1M" ::/overlays &&
    mcopy -i "$tmp/pi3.img@@1M" "$tmp/disable-bt.dtbo" ::/overlays/
result $? "pi3: firmware-style long file names added to the card"
log="$tmp/pi3.log"
: > "$log"
feed_pi3() {
    to_shell "$log" 1 || return
    cmd "$log" 'ls @:/'
    cmd "$log" 'write @:/PI.TXT written on the sd card'
    cmd "$log" 'cat @:/PI.TXT'
    printf 'reboot\r'
    to_shell "$log" 2 || return
    cmd "$log" 'cat @:/PI.TXT'
    cmd "$log" 'help'
    cmd "$log" 'help'
    cmd "$log" 'cat @:/PI.TXT'
    sleep 2
    printf '\001c'
    sleep 1
    printf 'screendump %s\n' "$tmp/pi3.ppm"
    sleep 3
    printf 'quit\n'
    sleep 2
}
feed_pi3 | timeout $((timeout_s * 2 + 300)) "${raspi3b[@]}" -drive "file=$tmp/pi3.img,if=sd,format=raw" > "$log" 2>&1
tr -d '\r' < "$log" > "$tmp/pi3.txt"
expect pi3 '^SamOS aarch64: booting \(entered at EL2, running at EL1\)$' "entered at 0x200000 at EL2, as the Pi firmware does"
expect pi3 '^console: pl011 at 0x3f201000 \(stdout-path\)$' "console: the PL011 /chosen/stdout-path names, through /soc's ranges"
expect pi3 '^memory: 0x0 size 0x3c000000$' "memory from the device tree (the VideoCore's 64 MiB left out)"
expect pi3 '^gic: none; BCM2836 per-core controller at 0x40000000, ARM controller at 0x3f00b200$' "BCM2836 and BCM2835 interrupt controllers"
expect pi3 '^timer: [0-9]+ Hz counter .*EL1 physical timer INTID 17 \(DT interrupts\)$' "timer: CNTPNSIRQ of the per-core controller (arm,armv7-timer)"
expect pi3 '^timer: 10 ticks took' "the tick runs"
expect pi3 '^mailbox: at 0x3f00b880, bus addresses 0xc0000000 = RAM 0x0 ' "mailbox, bus addresses from /soc's dma-ranges"
expect pi3 '^rpi: board revision 0xa02082, ' "mailbox property call (board revision: a Pi 3 B)"
expect pi3 '^mailbox: framebuffer 1024x768, 32 bpp, BGR, pitch 4096, 3145728 bytes at bus 0x[0-9a-f]+ = pa 0x[0-9a-f]+$' "framebuffer from the VideoCore mailbox"
expect pi3 '^graphics: framebuffer format 0 \(BGRX\)$' "the framebuffer is the screen"
expect pi3 '^console: PL011 receive on INTID 121, bytes are keys$' "PL011 receive line through the ARM controller (GPU IRQ 57)"
expect pi3 '^sdhci: brcm,bcm2835-sdhci: SDHCI 3\.00 at 0x0*3f300000, base clock 52000 kHz \(capabilities\)' "SDHCI found from the device tree"
expect pi3 '^sdhci: brcm,bcm2835-sdhci: SDSC card, RCA 0x[0-9a-f]+, 4-bit bus, [0-9]+ kHz, PIO$' "SD card set up (CMD0/8, ACMD41, CMD2/3/9/7, ACMD6)"
expect pi3 '^sdhci: disk 0, 256 MiB$' "SD card is disk 0, size from the CSD"
expect pi3 '^disk 1 part SDHCI fs=FAT label=SAMOS +rw$' "its FAT32 partition mounted, writable"
for cpu in 1 2 3; do
    expect pi3 "^smp: cpu $cpu mpidr 0x$cpu online\$" "CPU $cpu released from its spin table"
done
expect pi3 '^smp: 4 cpus online$' "four CPUs online"
expect pi3 '^smp selftest: call, tlb shootdown, lock and atomic counts ok$' "SMP self-test (IPIs are mailbox writes)"
expect pi3 '^e2e: user selftest done$' "BLANK.ELF loaded from the SD card ran"
expect pi3 '^SHELL\.ELF +[0-9]+$' "ls @:/ on the SD card (long names beside)"
expect pi3 '^rebooting$' "reboot typed"
expect pi3 '^rpi: restart by the PM watchdog at 0x3f100000$' "restart through the PM watchdog (no PSCI)"
[ "$(grep -ac '^SamOS aarch64: booting' "$tmp/pi3.txt")" -eq 2 ]
result $? "pi3: the machine restarted and booted again"
sed -n '/^rpi: restart by the PM watchdog/,$p' "$tmp/pi3.txt" > "$tmp/pi3.after"
grep -aq '^written on the sd card$' "$tmp/pi3.after"
result $? "pi3: after the reboot the file reads back from the SD card"
[ "$(grep -ac '^written on the sd card$' "$tmp/pi3.txt")" -ge 3 ]
result $? "pi3: cat before and after the reboot"
no_crash pi3
[ "$(mtype -i "$tmp/pi3.img@@1M" ::/PI.TXT 2> /dev/null)" = "written on the sd card" ]
result $? "pi3: host: mtools reads the file SamOS wrote to the SD card"
if [ -s "$tmp/pi3.ppm" ]; then
    python3 -I "$root/tests64/e2e/screen_text.py" "$tmp/pi3.ppm" "$root/assets/sysfont.bmp" > "$tmp/pi3.screen" 2>&1
    grep -q 'written on the sd card' "$tmp/pi3.screen"
    result $? "pi3: screen: the shell's output on the mailbox framebuffer (screendump)"
else
    result 1 "pi3: screen: no screendump"
fi

# ---- sdhc: a 4 GiB card, block addressing ------------------------------
cp --sparse=always "$pi/sdcard.img" "$tmp/sdhc.img"
truncate -s 4G "$tmp/sdhc.img"
log="$tmp/sdhc.log"
: > "$log"
feed_sdhc() {
    to_shell "$log" 1 || return
    cmd "$log" 'write @:/SDHC.TXT block addressed'
    cmd "$log" 'cat @:/SDHC.TXT'
    printf '\001c'
    sleep 1
    printf 'quit\n'
    sleep 2
}
feed_sdhc | timeout $((timeout_s + 300)) "${raspi3b[@]}" -drive "file=$tmp/sdhc.img,if=sd,format=raw" > "$log" 2>&1
tr -d '\r' < "$log" > "$tmp/sdhc.txt"
expect sdhc '^sdhci: brcm,bcm2835-sdhci: SDHC/SDXC card, RCA 0x[0-9a-f]+, 4-bit bus, [0-9]+ kHz, PIO$' "an SDHC card (CCS: block addressing)"
expect sdhc '^sdhci: disk 0, 4096 MiB$' "4 GiB from the version 2 CSD"
expect sdhc '^disk 1 part SDHCI fs=FAT label=SAMOS +rw$' "partition mounted"
expect sdhc '^block addressed$' "write and read back"
no_crash sdhc
[ "$(mtype -i "$tmp/sdhc.img@@1M" ::/SDHC.TXT 2> /dev/null)" = "block addressed" ]
result $? "sdhc: host: mtools reads the file"

# ---- GICv2 on QEMU virt -------------------------------------------------
# boot <name> <machine options> <qemu args> <append> <ERE>...
boot() {
    local name="$1" machine="$2" args="$3" append="$4"
    shift 4
    local log="$tmp/$name.log"
    : > "$log"
    # shellcheck disable=SC2086
    qemu-system-aarch64 -machine "virt,gic-version=2$machine" -cpu cortex-a72 -m 512M -nographic \
        -accel tcg,thread=multi -kernel "$kernel" -initrd "$root/bin/aarch64/ramdisk.img" \
        -append "$append" $args < /dev/null > "$log" 2>&1 &
    local pid=$! waited=0
    while kill -0 "$pid" 2> /dev/null; do
        local all=1 re
        tr -d '\r' < "$log" > "$log.now"
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
    kill "$pid" 2> /dev/null
    wait "$pid" 2> /dev/null
    tr -d '\r' < "$log" > "$tmp/$name.txt"
}

selftest_ok='^smp selftest: call, tlb shootdown, lock and atomic counts ok$'
gicv2_re='^gic: GICv2 distributor at 0x8000000, CPU interface at 0x8010000, [0-9]+ INTIDs, [0-9] CPU interface\(s\)$'

boot v2smp "" "-smp 4" "samos.run=KTEST.ELF cpubusy 8 4000" '^CPUBUSY (OK|FAIL)$'
expect v2smp "$gicv2_re" "GICv2 found (arm,cortex-a15-gic)"
expect v2smp '^timer: .*EL1 physical timer INTID 30 \(DT interrupts\)$' "timer PPI from the GIC binding"
for cpu in 1 2 3; do
    expect v2smp "^smp: cpu $cpu mpidr 0x$cpu online\$" "CPU $cpu online (PSCI), its banked GIC registers set up"
done
expect v2smp '^smp: 4 cpus online$' "four CPUs online"
expect v2smp "$selftest_ok" "SMP self-test: SGIs by GICD_SGIR target list and back"
for cpu in 0 1 2 3; do
    busy=$(sed -nE "s/^CPUBUSY cpu $cpu busy ([0-9]+) idle ([0-9]+) .*/\1/p" "$tmp/v2smp.txt" | head -n 1)
    [ "${busy:-0}" -gt 0 ]
    result $? "v2smp: cpu $cpu counted ${busy:-no} busy ticks of its own timer (banked PPI)"
done
expect v2smp '^CPUBUSY 4 cpus, children together ran on 0xf$' "children ran on all four CPUs (reschedule SGIs)"
expect v2smp '^CPUBUSY OK$' "cpubusy"
no_crash v2smp

boot v2el2 ",virtualization=on" "-smp 2" "samos.run=HELLO.ELF" '^e2e: user selftest done$'
expect v2el2 '^SamOS aarch64: booting \(entered at EL2, running at EL1\)$' "entered at EL2"
expect v2el2 "$gicv2_re" "GICv2"
expect v2el2 '^smp: 2 cpus online$' "second CPU by PSCI over smc, from EL2"
expect v2el2 "$selftest_ok" "SMP self-test"
no_crash v2el2

truncate -s 64M "$tmp/data.img"
mkfs.fat -F 32 -s 1 -n DATAVOL "$tmp/data.img" > /dev/null
log="$tmp/v2shell.log"
: > "$log"
feed_v2() {
    to_shell "$log" 1 || return
    cmd "$log" 'echo typed over serial on gicv2'
    cmd "$log" 'write @:/V2.TXT boot disk'
    cmd "$log" 'cat @:/V2.TXT'
    cmd "$log" 'write 1:/V2DATA.TXT second disk'
    cmd "$log" 'cat 1:/V2DATA.TXT'
    printf '\001c'
    sleep 1
    printf 'quit\n'
    sleep 2
}
feed_v2 | timeout $((timeout_s + 300)) qemu-system-aarch64 -machine virt,gic-version=2 -cpu cortex-a72 -m 512M \
    -smp 4 -accel tcg,thread=multi -nographic -global virtio-mmio.force-legacy=false -kernel "$kernel" \
    -drive "file=$root/bin/aarch64/os.img,format=raw,if=none,id=os,snapshot=on" -device virtio-blk-device,drive=os \
    -drive "file=$tmp/data.img,format=raw,if=none,id=data" -device virtio-blk-device,drive=data \
    -device ramfb > "$log" 2>&1
tr -d '\r' < "$log" > "$tmp/v2shell.txt"
expect v2shell "$gicv2_re" "GICv2"
expect v2shell '^smp: 4 cpus online$' "four CPUs"
[ "$(grep -acE '^disk: virtio-blk completions by interrupt, GSI [0-9]+ edge active-high, vector [0-9]+$' "$tmp/v2shell.txt")" -eq 2 ]
result $? "v2shell: both virtio disks complete by interrupt (SPIs on the GICv2)"
expect v2shell '^console: PL011 receive on INTID 33, bytes are keys$' "PL011 receive SPI"
expect v2shell '^typed over serial on gicv2$' "a line typed over serial"
expect v2shell '^boot disk$' "write, cat on the boot disk"
expect v2shell '^second disk$' "write, cat on the second disk"
no_crash v2shell
[ "$(mtype -i "$tmp/data.img" ::/V2DATA.TXT 2> /dev/null)" = "second disk" ]
result $? "v2shell: host: mtools reads the file"

if [ $failures -gt 0 ]; then
    for n in pi3 sdhc v2smp v2el2 v2shell; do
        [ -f "$tmp/$n.txt" ] && { echo "---- $n serial (tail) ----"; tail -n 40 "$tmp/$n.txt"; }
    done
    [ -f "$tmp/pi3.screen" ] && { echo "---- pi3 screen ----"; grep -v '^$' "$tmp/pi3.screen" | tail -n 20; }
fi
echo "aarch64-pi arch test: $checks checks, $failures failures"
[ $failures -eq 0 ]
