#!/usr/bin/env bash
# e2e/210-sdhci-pci.sh - the SD card driver of the Raspberry Pi
# (disk/drivers/sdhci.c) on another host: QEMU's sdhci-pci (PCI class
# 08/05, BAR 0 assigned by the firmware) with an SD card holding a
# FAT32 volume (64 MiB: a byte-addressed SDSC card). The card is set
# up (CMD0/8, ACMD41, CMD2/3/9/7, ACMD6), becomes a disk, its FAT32
# mounts; the shell writes a file on it and reads it back, and mtools
# reads it on the host.
# e2e-requires: x86 (the test boots -machine pc with OVMF; sdhci-pci on the ports, whose BARs the shared allocator now assigns, is not tried yet)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"

sd="$E2E_TMP/sd.img"
rm -f "$sd"
truncate -s 64M "$sd"
mkfs.fat -F 32 -s 1 -n SDCARD "$sd" > /dev/null || e2e_fail "mkfs.fat"
export E2E_EXTRA_QEMU_ARGS="-device sdhci-pci -drive file=$sd,if=none,id=sd,format=raw -device sd-card,drive=sd"

drive=""
sd_session() {
    shell_boot
    shell_cmd "write $drive:/SD.TXT on the sd card"
    shell_cmd "cat $drive:/SD.TXT"
    echo quit
}

# The SD card's drive number: the disk line the kernel prints.
boot_and_dump 25
id=$(tr -d '\r' < "$E2E_SERIAL" | sed -nE 's/^disk ([0-9]+) real SDHCI fs=FAT label=SDCARD +rw$/\1/p' | head -n 1)
expect_serial '^sdhci: pci: SDHCI [0-9]\.00 at 0x[0-9a-f]+, base clock [0-9]+ kHz' "sdhci-pci found on PCI"
expect_serial '^sdhci: pci: SDSC card, RCA 0x[0-9a-f]+, 4-bit bus, [0-9]+ kHz, PIO$' "SD card set up"
expect_serial '^sdhci: disk [0-9]+, 64 MiB$' "SD card is a disk"
[ -n "$id" ] || e2e_fail "no 'disk N real SDHCI fs=FAT label=SDCARD rw' line"
e2e_log "SD card FAT32 volume is disk $id"

drive=$id
e2e_run_qemu 180 sd_session
expect_serial '^on the sd card$' "write then cat on the SD card"
[ "$(MTOOLS_SKIP_CHECK=1 mtype -i "$sd" ::/SD.TXT 2> /dev/null)" = "on the sd card" ] ||
    e2e_fail "host: mtools does not read SD.TXT"
echo "PASS: e2e/210 SD card on sdhci-pci: set up, FAT32 mounted, a file written and read back"
