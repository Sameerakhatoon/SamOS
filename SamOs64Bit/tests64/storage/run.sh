#!/usr/bin/env bash
# tests64/storage/run.sh - storage end-to-end test.
#
# Usage: bash tests64/storage/run.sh [pc|q35|pc-virtio|q35-virtio ...]
#        (default: pc q35 q35-virtio)
#
# For each machine type it builds fresh scratch images, boots
# SamOS twice under OVMF, and checks the result:
#
#   boot.img   GPT: ESP (loader, kernel) + SAMOS partition
#   fat16.img  64 MiB FAT16, whole disk (no partition table)
#   fat32.img  160 GiB sparse, GPT, a 64 MiB FAT32 partition at
#              150 GiB: past the LBA28 limit, so PATA must use
#              LBA48 and every offset must be 64-bit
#   fat12.img  8 MiB, MBR partition table, FAT12
#
# All three scratch volumes are labelled SMTEST; the kernel's
# storage_selftest only ever writes to volumes with that label.
#
#   1. Linux (mtools) writes /FROMLNX files and TESTCMD.TXT=WRITE.
#   2. Boot 1: SamOS writes files, reads them back, reads the
#      Linux files, writes RESULT1.TXT.
#   3. Host: RESULT1 has no FAIL, fsck.fat is clean, and the
#      files SamOS wrote read back correctly through mtools.
#   4. TESTCMD.TXT=VERIFY, boot 2 (a reboot): SamOS checks that
#      everything survived and writes RESULT2.TXT + AFTERBOOT.TXT.
#   5. Host: RESULT2 has no FAIL, fsck.fat is clean again.
#
# On pc the four disks are IDE (primary/secondary, master/slave);
# on q35 they are AHCI ports 0-3. A machine written
# <pc|q35>-virtio (e.g. q35-virtio) puts all four on virtio-blk-pci,
# booting from the first.
#
# Needs: qemu-system-x86_64, OVMF, mtools, dosfstools, gdisk
# (sgdisk), fdisk (sfdisk), python3; plus a built kernel.bin and
# SamOs.efi (see SAMOS_EFI below). No sudo.

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
K64="$(cd "$HERE/../.." && pwd)"
# A fresh directory per run: worktrees run this test at the same time,
# and a shared default made one run delete another's disk images.
if [ -n "${STORAGE_WORK:-}" ]; then
    WORK="$STORAGE_WORK"
else
    WORK="$(mktemp -d "${TMPDIR:-/tmp}/samos-storage-test.XXXXXX")"
    [ "${STORAGE_KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' EXIT
fi
OVMF="${OVMF:-/usr/share/ovmf/OVMF.fd}"
# build.sh copies the loader to <repo>/bin/SamOs.efi; the EDK2 build
# tree is shared by every worktree, so do not read it from there.
SAMOS_EFI="${SAMOS_EFI:-$K64/../bin/SamOs.efi}"
TIMEOUT="${STORAGE_TIMEOUT:-300}"
export MTOOLS_SKIP_CHECK=1

FAT32_START=314572800          # LBA of the FAT32 partition (150 GiB)
FAT12_START=2048

fail_count=0
log()  { echo "[storage] $*"; }
bad()  { echo "[storage] FAIL: $*"; fail_count=$((fail_count + 1)); }

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing tool: $1" >&2; exit 2; }; }
for t in qemu-system-x86_64 mcopy mtype mmd mkfs.fat fsck.fat sgdisk sfdisk python3; do need "$t"; done
[ -f "$OVMF" ] || { echo "missing OVMF at $OVMF" >&2; exit 2; }
[ -f "$SAMOS_EFI" ] || { echo "missing SamOs.efi at $SAMOS_EFI (set SAMOS_EFI)" >&2; exit 2; }
[ -f "$K64/bin/kernel.bin" ] || { echo "missing $K64/bin/kernel.bin; build first" >&2; exit 2; }

# mtools image spec for each scratch volume.
spec() {
    case "$1" in
        fat16) echo "$WORK/fat16.img" ;;
        fat32) echo "$WORK/fat32.img@@$((FAT32_START * 512))" ;;
        fat12) echo "$WORK/fat12.img@@$((FAT12_START * 512))" ;;
    esac
}

make_boot_img() {
    local img="$WORK/boot.img"
    rm -f "$img"
    truncate -s 128M "$img"
    sgdisk -o -n 1:2048:+48M -t 1:ef00 -c 1:ESP -n 2:0:0 -t 2:0700 -c 2:SAMOS "$img" >/dev/null
    local p1 p1e p2 p2e
    p1=$(sgdisk -i 1 "$img" | awk '/First sector/{print $3}')
    p1e=$(sgdisk -i 1 "$img" | awk '/Last sector/{print $3}')
    p2=$(sgdisk -i 2 "$img" | awk '/First sector/{print $3}')
    p2e=$(sgdisk -i 2 "$img" | awk '/Last sector/{print $3}')
    mkfs.fat -F 16 -n ABC   --offset "$p1" "$img" $(( (p1e - p1 + 1) / 2 )) >/dev/null
    mkfs.fat -F 16 -n SAMOS --offset "$p2" "$img" $(( (p2e - p2 + 1) / 2 )) >/dev/null
    local esp="$img@@$((p1 * 512))" sam="$img@@$((p2 * 512))"
    mmd -i "$esp" ::EFI ::EFI/BOOT
    mcopy -i "$esp" "$SAMOS_EFI" ::EFI/BOOT/BOOTX64.EFI
    mcopy -i "$esp" "$K64/bin/kernel.bin" ::kernel.bin
    for dst in "$esp" "$sam"; do
        for f in programs/blank/blank.elf:BLANK.ELF programs/shell/shell.elf:SHELL.ELF \
                 assets/sysfont.bmp:sysfont.bmp; do
            [ -f "$K64/${f%%:*}" ] && mcopy -i "$dst" "$K64/${f%%:*}" "::${f##*:}"
        done
    done
}

make_scratch_imgs() {
    rm -f "$WORK/fat16.img" "$WORK/fat32.img" "$WORK/fat12.img"

    truncate -s 64M "$WORK/fat16.img"
    mkfs.fat -F 16 -n SMTEST "$WORK/fat16.img" >/dev/null

    truncate -s 160G "$WORK/fat32.img"
    sgdisk -o -n 1:$FAT32_START:+64M -t 1:0700 -c 1:SMTEST "$WORK/fat32.img" >/dev/null
    mkfs.fat -F 32 -s 1 -n SMTEST --offset $FAT32_START "$WORK/fat32.img" 65536 >/dev/null

    truncate -s 8M "$WORK/fat12.img"
    echo "start=$FAT12_START, type=01" | sfdisk -q "$WORK/fat12.img" >/dev/null
    mkfs.fat -F 12 -s 4 -n SMTEST --offset $FAT12_START "$WORK/fat12.img" \
        $(( (8 * 2048 - FAT12_START) / 2 )) >/dev/null

    printf 'written by mtools on linux\n' > "$WORK/linux.txt"
    python3 "$HERE/pattern.py" gen "$WORK/linuxbig.bin" 70000 3
    for v in fat16 fat32 fat12; do
        local s; s=$(spec $v)
        mmd -i "$s" ::FROMLNX
        mcopy -i "$s" "$WORK/linux.txt" "::FROMLNX/Linux Long Name File.txt"
        mcopy -i "$s" "$WORK/linuxbig.bin" ::FROMLNX/linuxbig.bin
        set_cmd "$v" WRITE
    done
}

set_cmd() {
    printf '%s\n' "$2" > "$WORK/cmd.txt"
    mcopy -o -i "$(spec "$1")" "$WORK/cmd.txt" ::TESTCMD.TXT
}

result_done() {
    mtype -i "$(spec "$1")" "::$2" 2>/dev/null | grep -q '^DONE '
}

run_qemu() {
    local machine=$1 tag=$2 result=$3
    # "<machine>-virtio": the same four disks on virtio-blk-pci.
    local bus=native
    if [ "${machine%-virtio}" != "$machine" ]; then
        bus=virtio
        machine=${machine%-virtio}
    fi
    # STORAGE_SMP CPUs (default 1), one host thread each.
    local smp=${STORAGE_SMP:-1} accel=tcg
    [ "$smp" -gt 1 ] && accel=tcg,thread=multi
    local args=(-machine "$machine" -m 512M -cpu Skylake-Server -bios "$OVMF"
                -smp "$smp" -accel "$accel"
                -display none -no-reboot -monitor none
                -serial "file:$WORK/serial-$tag.log")
    local i=0
    for d in boot fat16 fat32 fat12; do
        args+=(-drive "file=$WORK/$d.img,format=raw,if=none,id=d$i")
        if [ "$bus" = virtio ]; then
            args+=(-device "virtio-blk-pci,drive=d$i$([ $i -eq 0 ] && echo ,bootindex=0)")
        elif [ "$machine" = pc ]; then
            args+=(-device "ide-hd,drive=d$i,bus=ide.$((i / 2)),unit=$((i % 2))")
        else
            args+=(-device "ide-hd,drive=d$i,bus=ide.$i")
        fi
        i=$((i + 1))
    done
    log "boot $tag ($machine)"
    qemu-system-x86_64 "${args[@]}" > "$WORK/qemu-$tag.log" 2>&1 &
    local pid=$! t=0
    while kill -0 "$pid" 2>/dev/null && [ $t -lt "$TIMEOUT" ]; do
        sleep 3
        t=$((t + 3))
        if grep -q 'KERNEL PANIC' "$WORK/serial-$tag.log" 2>/dev/null; then
            bad "$tag: kernel panic, see $WORK/serial-$tag.log"
            break
        fi
        if result_done fat16 "$result" && result_done fat32 "$result" && result_done fat12 "$result"; then
            sleep 2
            break
        fi
    done
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    log "boot $tag finished after ${t}s"
}

check_result() {
    local v=$1 file=$2 tag=$3
    local out; out=$(mtype -i "$(spec "$v")" "::$file" 2>/dev/null)
    if ! grep -q '^DONE ' <<<"$out"; then
        bad "$tag $v: $file missing or incomplete"
        return
    fi
    if grep -q '^FAIL' <<<"$out" || ! grep -q 'fail=0$' <<<"$out"; then
        bad "$tag $v: $file reports failures:"
        grep '^FAIL' <<<"$out" | sed 's/^/    /'
    fi
    # On a -virtio machine the volume must have gone through virtio-blk.
    if [ "${tag%%-virtio *}" != "$tag" ] && ! grep -q 'driver=VIRTIO-BLK' <<<"$out"; then
        bad "$tag $v: not read through virtio-blk"
    fi
    log "$tag $v: $(grep '^INFO' <<<"$out") $(grep '^DONE' <<<"$out")"
}

fsck_volume() {
    local v=$1 tag=$2 part="$WORK/fsck.img"
    case $v in
        fat16) cp --sparse=always "$WORK/fat16.img" "$part" ;;
        fat32) dd if="$WORK/fat32.img" of="$part" bs=512 skip=$FAT32_START count=$((64 * 2048)) status=none ;;
        fat12) dd if="$WORK/fat12.img" of="$part" bs=512 skip=$FAT12_START status=none ;;
    esac
    if ! fsck.fat -n "$part" > "$WORK/fsck-$v-$tag.log" 2>&1; then
        bad "$tag $v: fsck.fat found errors"
        sed 's/^/    /' "$WORK/fsck-$v-$tag.log" | head -20
    fi
    rm -f "$part"
}

check_tree() {
    local v=$1 tag=$2 dir="$WORK/tree-$v"
    rm -rf "$dir"
    mkdir -p "$dir"
    if ! mcopy -s -n -i "$(spec "$v")" "::*" "$dir/" 2>"$WORK/mcopy-$v.log"; then
        bad "$tag $v: mcopy of the volume failed"
        return
    fi
    if ! python3 "$HERE/pattern.py" tree "$dir" > "$WORK/tree-$v.log"; then
        bad "$tag $v: files written by SamOS do not match on Linux:"
        sed 's/^/    /' "$WORK/tree-$v.log"
    fi
}

run_machine() {
    local machine=$1
    log "=== $machine ==="
    # A fresh boot image per machine: OVMF without a pflash varstore
    # saves its variables to \NvVars on the ESP, and the q35 boot
    # entries it leaves there make the next pc boot (or the reverse)
    # drop into the UEFI shell instead of BOOTX64.EFI.
    make_boot_img
    make_scratch_imgs

    run_qemu "$machine" "$machine-1" RESULT1.TXT
    for v in fat16 fat32 fat12; do
        check_result "$v" RESULT1.TXT "$machine boot1"
        fsck_volume "$v" "$machine-1"
        check_tree "$v" "$machine boot1"
        set_cmd "$v" VERIFY
    done

    run_qemu "$machine" "$machine-2" RESULT2.TXT
    for v in fat16 fat32 fat12; do
        check_result "$v" RESULT2.TXT "$machine boot2"
        fsck_volume "$v" "$machine-2"
        if [ "$(mtype -i "$(spec "$v")" ::SUBDIR/AFTERBOOT.TXT 2>/dev/null)" != "second boot" ]; then
            bad "$machine boot2 $v: AFTERBOOT.TXT not readable on Linux"
        fi
    done
}

mkdir -p "$WORK"
machines=("$@")
[ ${#machines[@]} -eq 0 ] && machines=(pc q35 q35-virtio)
for m in "${machines[@]}"; do
    run_machine "$m"
done

if [ $fail_count -eq 0 ]; then
    echo "storage: PASS (${machines[*]})"
    exit 0
fi
echo "storage: $fail_count failure(s)"
exit 1
