#!/usr/bin/env bash
# e2e/197-virtio-blk-smp.sh - boot from virtio-blk-pci with four
# CPUs, on q35 and pc. OVMF loads the kernel from the virtio disk, the
# kernel finds @: (the SAMOS partition) on it, and every program and
# file read goes through virtio_blk.c. Then e2e/194's load: KTEST.ELF
# readers 4 2 BIG.BIN H, four readers hashing a 1 MiB file twice, from
# whichever CPU each runs on, under the one disk lock.
# Expect, from the boot markers after the reads:
#   - 192 > 0: virtio-blk completions taken at the interrupt;
#   - 190 > 0: an AP waited and took its own completion (the line is
#     steered to the waiting CPU);
#   - 191 = 0: none taken by another CPU; 187 unset: none polled;
#   - every hash right, no fault, no lock report.
# S-PAR - on aarch64 virt the kernel comes from -kernel and the boot
# disk is the only virtio-blk-pci (E2E_BOOT_BUS=virtio in the hook), its
# INTx a GIC SPI steered to the waiting CPU. Parity - riscv64 virt the
# same, its INTx a PLIC source steered to the waiting hart's context.
# e2e-requires: smp virtio-pci (booting from virtio-blk-pci with four CPUs)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_virtio_lib.sh"

export E2E_SMP=4
export E2E_BOOT_BUS=virtio
big="$E2E_TMP/BIG.BIN"
head -c $((1024 * 1024)) /dev/urandom > "$big"
h=$(python3 -c 'import sys
h = 2166136261
for b in open(sys.argv[1], "rb").read():
    h = ((h ^ b) * 16777619) & 0xffffffff
print("%x" % h)' "$big")
cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF readers 4 2 big.bin %s\n' "$h" > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"
p2=$(( $(stat -c %s "$E2E_IMAGE") / 512 / 2 ))
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((p2 * 512))" "$big" ::/BIG.BIN \
    || e2e_fail "mcopy BIG.BIN"

machines="q35 pc"
[ "$E2E_ARCH" = aarch64 ] && machines=virt
[ "$E2E_ARCH" = riscv64 ] && machines=virt
for machine in $machines; do
    E2E_MACHINE=$machine
    export E2E_MACHINE
    _script() {
        e2e_wait_serial '^READERS (OK|FAIL)$' 800 || true
        sleep 1
        e2e_pmemsave
        sleep 1
        printf 'quit\n'
    }
    rm -f "$E2E_DUMP"
    e2e_run_qemu 900 _script
    e2e_log "machine $machine"
    expect_serial '^smp: 4 cpus online$' "$machine: four CPUs online"
    virtio_blk_serial_check "$E2E_SERIAL" "$machine" 0
    expect_serial '^e2e: user selftest done' "$machine: BLANK.ELF loaded from the virtio disk"
    expect_serial '^READERS 4 readers 2 passes in [0-9]+ ms$' "$machine: four readers finished"
    expect_serial '^READERS OK$' "$machine: every hash right"
    expect_no_serial 'READERS .*FAIL|STRESS reader|KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' \
        "$machine: no failure, no fault"
    [ -s "$E2E_DUMP" ] || e2e_fail "$machine: no marker dump"
    irq=$(virtio_count 192); ap=$(virtio_count 190); other=$(virtio_count 191); polled=$(virtio_count 187)
    e2e_log "$machine: virtio-blk completions by interrupt $irq, taken by the AP that waited $ap, by another CPU $other, polled $polled"
    [ "$irq" -gt 0 ] || e2e_fail "$machine: no virtio-blk completion by interrupt"
    [ "$ap" -gt 0 ] || e2e_fail "$machine: no AP took its own disk completion"
    [ "$other" -eq 0 ] || e2e_fail "$machine: $other completions taken by a CPU other than the waiter"
    [ "$polled" -eq 0 ] || e2e_fail "$machine: $polled waits fell back to polling"
done
echo "PASS: e2e/197 boot from virtio-blk, parallel reads on four CPUs by interrupt ($machines)"
