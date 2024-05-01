#!/usr/bin/env bash
# e2e/194-smp-disk-irq.sh - with the platform code's disk interrupts: parallel file reads on four CPUs complete by interrupt, on
# q35 (AHCI, GSI 16) and pc (PATA, IRQ14).
#
# KTEST.ELF readers 4 2 BIG.BIN H: four readers at once, each hashing
# a 1 MiB random file twice. The file is twice the size of the sector
# cache, so every pass goes to the disk, from whichever CPU the reader
# runs on. One disk lock serialises the drivers; a wait steers the
# line to its own CPU before the command goes out (disk_irq_arm).
# Expect, from the boot markers after the reads:
#   - 190 > 0: an AP waited for a completion and took the interrupt
#     itself (not the BSP, where the line is routed at boot);
#   - 191 = 0: no completion was taken by a CPU other than the waiter;
#   - 187 unset: no wait fell back to polling, and no "completion
#     interrupt missing" line;
#   - every hash right ("READERS OK"), no fault, no lock report.
# e2e-requires: smp ahci ide (four CPUs with AHCI and PATA interrupts)
. "$(dirname "$0")/_lib.sh"

export E2E_SMP=4
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
# @: is the second partition (build.sh: P2 starts half way in), where
# KTEST.ELF lives.
p2=$(( $(stat -c %s "$E2E_IMAGE") / 512 / 2 ))
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((p2 * 512))" "$big" ::/BIG.BIN \
    || e2e_fail "mcopy BIG.BIN"

for machine in q35 pc; do
    E2E_MACHINE=$machine
    export E2E_MACHINE
    if [ "$machine" = q35 ]; then marker=185; else marker=186; fi
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
    expect_serial '^disk: .* completions by interrupt, GSI ' "$machine: disk line routed through the IOAPIC"
    expect_serial '^READERS 4 readers 2 passes in [0-9]+ ms$' "$machine: four readers finished"
    expect_serial '^READERS OK$' "$machine: every hash right"
    expect_no_serial 'READERS .*FAIL|STRESS reader|completion interrupt missing|KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' \
        "$machine: no failure, no fallback to polling, no fault"
    [ -s "$E2E_DUMP" ] || e2e_fail "$machine: no marker dump"
    count() { [ "$(marker_high "$1")" = "$(expected_high)" ] && marker_value "$1" || echo 0; }
    irq=$(count "$marker"); ap=$(count 190); other=$(count 191); polled=$(count 187)
    e2e_log "$machine: completions by interrupt $irq, taken by the AP that waited $ap, by another CPU $other, polled $polled"
    [ "$irq" -gt 0 ] || e2e_fail "$machine: no completion by interrupt"
    [ "$ap" -gt 0 ] || e2e_fail "$machine: no AP took its own disk completion"
    [ "$other" -eq 0 ] || e2e_fail "$machine: $other completions taken by a CPU other than the waiter"
    [ "$polled" -eq 0 ] || e2e_fail "$machine: $polled waits fell back to polling"
done
echo "PASS: e2e/194 parallel reads on four CPUs complete by interrupt on the CPU that waits"
