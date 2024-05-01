#!/usr/bin/env bash
# e2e/204-usb-storage.sh - a FAT16 stick on usb-storage (bulk-only
# transport, SCSI) mounted as drive 1:.
#   1. q35: the shell reads a file Linux put there, writes a file, makes
#      a directory and writes into it, reads both back.
#   2. pc, the same stick after a power cycle (a second QEMU): both files
#      are there, a file is removed. Linux then finds the written files
#      with mtools, the removed one gone, and fsck.fat finds no error.
#   3. q35 -smp 4: KTEST.ELF's four readers hash a 1 MiB file on the
#      stick twice each (BOT transfers from every CPU); every hash right,
#      transfers ended at the MSI-X interrupt on the CPU that waited
#      (194 > 0, 191 and 187 unset).
#   4. readonly=on: MODE SENSE reports write protection, the disk is
#      read-only, a write is refused, the image is unchanged.
# e2e-requires: xhci (qemu-xhci on PCI, x86 machines and OVMF; on aarch64 virt nothing assigns PCI BARs and there is no MSI)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_usb_lib.sh"

export MTOOLS_SKIP_CHECK=1
stick="$E2E_TMP/stick.img"
usb_stick "$stick" USBSTICK

_rw1_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'cat 1:/USBHELLO.TXT'
    shell_cmd 'echo written through usb > 1:/NEW.TXT'
    shell_cmd 'mkdir 1:/DIR'
    shell_cmd 'write 1:/DIR/F.TXT in a usb directory'
    shell_cmd 'cat 1:/NEW.TXT'
    shell_cmd 'cat 1:/DIR/F.TXT'
    printf 'quit\n'
}
E2E_MACHINE=q35
usb_args "" "$stick"
e2e_run_qemu 300 _rw1_mon
usb_enum_check "boot 1" stick
expect_serial '^hello from the usb stick$' "boot 1: read a Linux file"
expect_serial '^written through usb$' "boot 1: echo > file read back"
expect_serial '^in a usb directory$' "boot 1: file in a new directory read back"
cp "$E2E_SERIAL" "$E2E_TMP/serial-1.log"

_rw2_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'cat 1:/NEW.TXT'
    shell_cmd 'cat 1:/DIR/F.TXT'
    shell_cmd 'rm 1:/USBHELLO.TXT'
    shell_cmd 'ls 1:/'
    printf 'quit\n'
}
E2E_MACHINE=pc
e2e_run_qemu 300 _rw2_mon
usb_enum_check "boot 2" stick
expect_serial '^written through usb$' "boot 2: the file survived the power cycle"
expect_serial '^in a usb directory$' "boot 2: the directory survived"
expect_no_serial '^USBHELLO\.TXT' "boot 2: rm removed USBHELLO.TXT"

[ "$(mtype -i "$stick" ::/NEW.TXT | tr -d '\r')" = "written through usb" ] || e2e_fail "host: NEW.TXT is not what the guest wrote"
mtype -i "$stick" ::/DIR/F.TXT | grep '^in a usb directory' > /dev/null || e2e_fail "host: DIR/F.TXT is not what the guest wrote"
if mdir -i "$stick" ::/USBHELLO.TXT >/dev/null 2>&1; then
    e2e_fail "host: USBHELLO.TXT still there"
fi
fsck.fat -n "$stick" > "$E2E_TMP/fsck.log" 2>&1 || { cat "$E2E_TMP/fsck.log" >&2; e2e_fail "host: fsck.fat found errors"; }
e2e_log "host: files as written, fsck.fat clean"

big="$E2E_TMP/BIG.BIN"
head -c $((1024 * 1024)) /dev/urandom > "$big"
h=$(python3 -I -c 'import sys
h = 2166136261
for b in open(sys.argv[1], "rb").read():
    h = ((h ^ b) * 16777619) & 0xffffffff
print("%x" % h)' "$big")
mcopy -o -i "$stick" "$big" ::/BIG.BIN || e2e_fail "mcopy BIG.BIN"
usb_cmdline "samos.run=KTEST.ELF readers 4 2 1:/BIG.BIN $h"
_readers_mon() {
    e2e_wait_serial '^READERS (OK|FAIL)$' 800 || true
    sleep 1
    printf 'pmemsave 0x6000 %d "%s"\n' "$E2E_MARKER_REGION_SIZE" "$E2E_DUMP"
    sleep 1
    printf 'quit\n'
}
E2E_MACHINE=q35
E2E_SMP=4
E2E_EXTRA_QEMU_ARGS="-device qemu-xhci,id=xhci -drive file=$stick,format=raw,if=none,id=stick -device usb-storage,bus=xhci.0,drive=stick"
rm -f "$E2E_DUMP"
e2e_run_qemu 900 _readers_mon
E2E_SMP=1
E2E_IMAGE=$E2E_OS_IMG
expect_serial '^smp: 4 cpus online$' "smp: four CPUs online"
expect_serial '^xhci: interrupts by MSI-X, vector 92$' "smp: MSI-X"
expect_serial '^READERS OK$' "smp: every hash right"
expect_no_serial 'READERS .*FAIL|STRESS reader|completion interrupt missing|KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:|disk disabled' \
    "smp: no failure, no fallback to polling, no fault"
[ -s "$E2E_DUMP" ] || e2e_fail "smp: no marker dump"
irq=$(usb_count 194); ap=$(usb_count 190); other=$(usb_count 191); polled=$(usb_count 187)
e2e_log "smp: stick transfers at the interrupt $irq, taken by the AP that waited $ap, by another CPU $other, polled $polled"
[ "$irq" -gt 100 ] || e2e_fail "smp: only $irq transfers at the interrupt"
[ "$ap" -gt 0 ] || e2e_fail "smp: no AP took its own completion"
[ "$other" -eq 0 ] || e2e_fail "smp: $other completions taken by a CPU other than the waiter"
[ "$polled" -eq 0 ] || e2e_fail "smp: $polled waits fell back to polling"

before=$(md5sum < "$stick")
_ro_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'cat 1:/NEW.TXT'
    shell_cmd 'echo no > 1:/NO.TXT'
    printf 'quit\n'
}
E2E_EXTRA_QEMU_ARGS="-device qemu-xhci,id=xhci -drive file=$stick,format=raw,if=none,id=stick,readonly=on -device usb-storage,bus=xhci.0,drive=stick"
e2e_run_qemu 300 _ro_mon
expect_serial '^usb-msc: slot [0-9]+, QEMU QEMU HARDDISK, disk 1, 32 MiB, write-protected$' "readonly: write protection seen"
expect_serial '^written through usb$' "readonly: read works"
expect_serial '^echo: 1:/NO\.TXT: read-only$' "readonly: the write was refused"
[ "$(md5sum < "$stick")" = "$before" ] || e2e_fail "readonly: the image changed"
echo "PASS: e2e/204 USB stick: read, write, mkdir across a power cycle (host-checked), 4-CPU readers by MSI-X, write protection"
