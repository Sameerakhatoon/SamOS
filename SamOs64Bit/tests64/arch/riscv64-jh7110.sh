#!/usr/bin/env bash
# tests64/arch/riscv64-jh7110.sh - what of the VisionFive 2 /
# Milk-V Mars (JH7110) port QEMU can show. Run after
#   bash arch-build.sh riscv64
#
#   1. The Image header U-Boot's booti reads (boot.S): code0 jumps past
#      it, text_offset 2 MiB, image_size the whole image (ELF
#      __kernel_end - __kernel_start, BSS and pools included), flags
#      little endian, version 0.2, both magics. Every other riscv64
#      test boots this same file with -kernel.
#   2. The Sv39-only rule: QEMU virt (whose rv64 has Sv48) with the
#      root compatible of a VisionFive 2 (dtb-jh7110.py --jh7110):
#      the kernel does not probe Sv48 and runs in Sv39.
#   3. A JH7110 stand-in: QEMU sifive_u has the same hart layout (hart 0
#      an M-mode-only monitor core, harts 1-4 with MMUs, a PLIC with
#      one context for hart 0 and two for the others, a SiFive cache
#      controller). Five harts, the VisionFive 2's compatible and
#      dma-noncoherent on /soc: Sv39 by the rule, hart 0 skipped,
#      harts 1-4 online (OpenSBI boots one of them), the PLIC context
#      of the boot hart, DMA maintained through the cache controller's
#      Flush64 register, KTEST.ELF cpubusy on all four.
#   (between 2 and 3) virt with its UART made a "snps,dw-apb-uart" and a
#      U-Boot style simple-framebuffer: the console moves to it, the
#      framebuffer's memory is reserved and it becomes the screen.
#   4. virt with zicbom=true and samos.dma_noncoherent, booting from
#      virtio-blk: every DMA buffer goes through cbo.*.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
kernel="${RISCV64_KERNEL:-$root/bin/riscv64/kernel.img}"
initrd="${RISCV64_INITRD:-$root/bin/riscv64/initrd.img}"
elf="$root/build/riscv64/kernel.elf"
for f in "$kernel" "$initrd" "$elf"; do
    [ -f "$f" ] || { echo "FAIL: $f missing; run bash arch-build.sh riscv64" >&2; exit 2; }
done
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
fails=0
checks=0

pass() { checks=$((checks + 1)); echo "  ok: $1"; }
fail() { checks=$((checks + 1)); echo "  FAIL: $1"; fails=$((fails + 1)); }
check() { if [ "$1" -eq 0 ]; then pass "$2"; else fail "$2"; fi; }
has() { grep -aE "$1" "$log" > /dev/null; }

# boot <name> <until-regex> <seconds> <qemu args...>: serial to $log.
boot() {
    local name=$1 until=$2 secs=$3
    shift 3
    log="$tmp/$name.log"
    timeout $((secs + 20)) qemu-system-riscv64 "$@" -display none -monitor none -serial "file:$log.raw" \
        < /dev/null > "$tmp/$name-qemu.log" 2>&1 &
    local pid=$!
    for _ in $(seq $((secs * 10))); do
        [ -f "$log.raw" ] && tr -d '\r' < "$log.raw" | grep -aE "$until" > /dev/null && break
        kill -0 $pid 2> /dev/null || break
        sleep 0.1
    done
    kill $pid 2> /dev/null
    wait $pid 2> /dev/null
    tr -d '\r' < "$log.raw" > "$log" 2> /dev/null
}

echo "riscv64 Image header"
riscv64-linux-gnu-nm "$elf" > "$tmp/nm"
end=$(awk '$3 == "__kernel_end" { print $1 }' "$tmp/nm")
start=$(awk '$3 == "__kernel_start" { print $1 }' "$tmp/nm")
size=$((0x${end:-0} - 0x${start:-0}))
python3 -I - "$kernel" "${size:-0}" > "$tmp/hdr" << 'EOF'
import struct, sys
h = open(sys.argv[1], "rb").read(64)
code0, code1, text_offset, image_size, flags, version, res1, res2, magic, magic2, res3 = \
    struct.unpack("<IIQQQIIQ8s4sI", h)
print("code0", "ok" if code0 == 0x0400006f else hex(code0))
print("text_offset", hex(text_offset))
print("image_size", "ok" if image_size == int(sys.argv[2]) and image_size > 0 else "%d != %s" % (image_size, sys.argv[2]))
print("flags", flags)
print("version", hex(version))
print("magic", magic.rstrip(b"\0").decode(), magic2.hex())
EOF
grep -q '^code0 ok$' "$tmp/hdr"
check $? "code0 is j +64 (past the header)"
grep -q '^text_offset 0x200000$' "$tmp/hdr"
check $? "text_offset 2 MiB"
grep -q '^image_size ok$' "$tmp/hdr"
check $? "image_size $size, the ELF's __kernel_end - __kernel_start"
grep -q '^flags 0$' "$tmp/hdr"
check $? "flags 0 (little endian)"
grep -q '^version 0x2$' "$tmp/hdr"
check $? "version 0.2"
grep -q '^magic RISCV 52534305$' "$tmp/hdr"
check $? "magics RISCV and RSC\\x05"

echo "riscv64 virt with the VisionFive 2's compatible"
qemu-system-riscv64 -machine virt,dumpdtb="$tmp/virt.dtb" -smp 2 -m 512M -display none > /dev/null 2>&1
python3 -I "$here/dtb-jh7110.py" edit "$tmp/virt.dtb" "$tmp/virt-jh.dtb" --jh7110
check $? "device tree with starfive,jh7110 at the root"
boot virt-sv39 '^SamOs v1\.0\.0 - type help|KERNEL PANIC' 90 -machine virt -cpu rv64 -smp 2 -m 512M \
    -bios default -dtb "$tmp/virt-jh.dtb" -kernel "$kernel" -initrd "$initrd" -append "samos.test=1"
has '^mmu: sv39 only on starfive,jh7110, sv48 not probed$'
check $? "sv48 not probed"
has '^mmu: sv39, 3 levels'
check $? "paging in sv39 on a cpu that has sv48"
has '^smp: 2 cpus online$'
check $? "both harts online"
has '^SamOs v1\.0\.0 - type help'
check $? "the shell started"
! has 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:'
check $? "no fault"

echo "riscv64 virt with a DesignWare UART and a U-Boot framebuffer"
python3 -I "$here/dtb-jh7110.py" edit "$tmp/virt.dtb" "$tmp/virt-fb.dtb" --dw-uart --simplefb 0x9c000000,640,480
check $? "device tree with snps,dw-apb-uart and a simple-framebuffer at 0x9c000000"
boot virt-fb '^SamOs v1\.0\.0 - type help|KERNEL PANIC' 90 -machine virt -cpu rv64 -smp 2 -m 512M \
    -bios default -dtb "$tmp/virt-fb.dtb" -kernel "$kernel" -initrd "$initrd" -append "samos.test=1"
has '^console: snps,dw-apb-uart at 0x10000000$'
check $? "console on the DesignWare UART"
has '^pfa: reserve 0x9c000000-0x9c12c000 simple-framebuffer$'
check $? "the framebuffer's memory kept from the frame allocator"
has '^simplefb: 640x480 a8r8g8b8, stride 2560, at 0x9c000000$'
check $? "the framebuffer taken as the screen"
has '^graphics: framebuffer format 0 \(BGRX\)$' && has '^graphics: framebuffer 640x480 pitch 640, mapped 1200 KiB of 1200 KiB'
check $? "graphics draws on it (BGRX, 640x480, all 1200 KiB mapped)"
has '^SamOs v1\.0\.0 - type help'
check $? "the shell started"
! has 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:'
check $? "no fault"

echo "riscv64 sifive_u as a JH7110 stand-in, five harts"
qemu-system-riscv64 -machine sifive_u,dumpdtb="$tmp/su.dtb" -smp 5 -m 1G -display none > /dev/null 2>&1
python3 -I "$here/dtb-jh7110.py" edit "$tmp/su.dtb" "$tmp/su-jh.dtb" --jh7110 --noncoherent
check $? "device tree with starfive,jh7110 and dma-noncoherent"
boot sifive-u '^CPUBUSY (OK|FAIL)|KERNEL PANIC' 240 -machine sifive_u -smp 5 -m 1G -accel tcg,thread=multi \
    -bios default -dtb "$tmp/su-jh.dtb" -kernel "$kernel" -initrd "$initrd" \
    -append "samos.test=1 samos.run=KTEST.ELF cpubusy 6 4000"
boot=$(sed -n 's/^boot hart: \([0-9]*\)$/\1/p' "$log" | head -n 1)
[ -n "$boot" ] && [ "$boot" != 0 ]
check $? "OpenSBI booted a hart with an MMU (hart $boot)"
has '^mmu: sv39 only on starfive,jh7110, sv48 not probed$'
check $? "sv39 by the board rule"
has '^smp: hart 0 skipped: (disabled in the device tree|no mmu-type)$'
check $? "hart 0, the monitor core, skipped: $(grep -a '^smp: hart 0 skipped' "$log" | head -n 1)"
for h in 1 2 3 4; do
    [ "$h" = "$boot" ] && continue
    has "^smp: cpu [123] hart $h online$"
    check $? "hart $h online"
done
has '^smp: 4 cpus online$'
check $? "four cpus online"
has '^plic: 0xc000000, [0-9]+ sources, context [0-9]+$'
check $? "the boot hart's PLIC context: $(grep -a '^plic: 0xc000000' "$log" | head -n 1)"
has '^dma: non-coherent \(dma-noncoherent in the device tree\), cache controller flush64 at 0x2010200, 64-byte lines$'
check $? "DMA by the cache controller's Flush64 (no Zicbom on these harts)"
has '^dma: self-test ok$'
check $? "DMA self-test"
has '^CPUBUSY 4 cpus, children together ran on 0xf$'
check $? "the children ran on all four harts"
has '^CPUBUSY OK$'
check $? "ktest cpubusy OK"
! has 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:|did not start'
check $? "no fault"

echo "riscv64 virt with Zicbom and samos.dma_noncoherent, from virtio-blk"
boot virt-zicbom '^SamOs v1\.0\.0 - type help|KERNEL PANIC' 90 -machine virt -cpu rv64,zicbom=true -smp 2 \
    -m 512M -bios default -kernel "$kernel" -append "samos.test=1 samos.dma_noncoherent samos.run=HELLO.ELF a b" \
    -global virtio-mmio.force-legacy=false -drive "file=$root/bin/riscv64/os.img,format=raw,if=none,id=os,snapshot=on" \
    -device virtio-blk-device,drive=os -device virtio-keyboard-device
has '^dma: non-coherent \(samos.dma_noncoherent\), zicbom, 64-byte blocks$'
check $? "cbo.* for DMA buffers"
has '^dma: self-test ok$'
check $? "DMA self-test (cbo.clean, cbo.inval, cbo.flush did not trap)"
has '^virtio-blk: disk 0, '
check $? "virtio-blk up"
has '^disk 2 part VIRTIO-BLK fs=FAT label=SAMOS'
check $? "its FAT partition mounted"
has '^hello: argv\[2\]=b$'
check $? "HELLO.ELF loaded from it and ran"
has '^SamOs v1\.0\.0 - type help'
check $? "the shell loaded from it"
! has 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:|illegal'
check $? "no fault"

# Summary line read by tests64/arch/run.sh (the arch tests' format).
echo "riscv64 arch test: $checks checks, $fails failures"
if [ $fails -eq 0 ]; then
    echo "PASS: tests64/arch/riscv64-jh7110"
else
    echo "FAIL: tests64/arch/riscv64-jh7110 ($fails failed)"
    exit 1
fi
