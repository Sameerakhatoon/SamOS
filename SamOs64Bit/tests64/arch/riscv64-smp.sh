#!/usr/bin/env bash
# tests64/arch/riscv64-smp.sh - the atomics the harts share, and
# a boot on three harts. Run after
#   bash arch-build.sh riscv64
#
#   1. atomics (static, on build/riscv64/kernel.elf): every copy of the
#      shared atomics (arch/atomic_gcc.h, compiled at -O0 in each
#      shared object that uses them) is the A extension with RVWMO's
#      seq_cst / acquire / release mapping: compare-and-swap lr.w.aqrl
#      + sc.w.rl, exchange amoswap.w.aqrl, add and dec amoadd.w.aqrl,
#      an acquire load lw + fence r,rw, a release store fence rw,w + sw;
#      spin_lock takes the lock with atomic_cas and spin_unlock drops
#      it with atomic_store; nothing calls libatomic (__atomic_* or
#      __sync_*); cpu_relax is pause (Zihintpause, a fence hint).
#   2. three harts (an odd count; OpenSBI boots any of them) in Sv48
#      and Sv39: every other hart online, the SMP self-test (calls,
#      RFENCE shootdown, lock and atomic counts on all harts at once),
#      the shell. The four-hart runs are e2e/190-193, 200, 201.
#   3. four harts from an edited device tree (dtb-harts.py, -dtb) in
#      Sv48 and Sv39: hart 1 disabled and a hart 7 that does not exist,
#      so the enabled ids are not contiguous and hart_start on one fails
#      (an SBI error, or no answer in the 5 s window). Each is reported
#      and skipped, the others fill slots 1 and 2 in tree order, the
#      boot hart (OpenSBI's lottery, often not hart 0) is cpu 0. Then
#      KTEST.ELF cpubusy: every CPU counts busy timer ticks of its own
#      and the children ran on all three.
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

echo "riscv64 atomics"
riscv64-linux-gnu-objdump -d --no-show-raw-insn "$elf" > "$tmp/dis"
# body <function>: the instructions of every copy, one per line, each
# copy ending in a line "--".
body() {
    awk -v f="<$1>:" '$2 == f { p = 1; next } p && NF == 0 { p = 0; print "--" } p { $1 = ""; print }' "$tmp/dis"
}
# expect_seq <function> <regex>...: each copy has the instructions in
# this order (other instructions between them allowed).
expect_seq() {
    local fn=$1
    shift
    local n IFS=';'
    n=$(body "$fn" | awk -v pats="$*" '
        BEGIN { k = split(pats, pat, ";"); i = 1 }
        $0 == "--" { copies++; if (i > k) good++; i = 1; next }
        i <= k && $0 ~ pat[i] { i++ }
        END { printf "%d %d\n", copies, good }')
    [ "${n% *}" -gt 0 ] && [ "${n% *}" = "${n#* }" ]
    check $? "$fn: $(echo "$*" | tr ';' ' ') (${n% *} copies, ${n#* } right)"
}
expect_seq atomic_cas '^ *lr[.]w[.]aqrl' '^ *bne' '^ *sc[.]w[.]rl' '^ *bnez'
expect_seq atomic_xchg '^ *amoswap[.]w[.]aqrl'
expect_seq atomic_add '^ *amoadd[.]w[.]aqrl'
expect_seq atomic_dec '^ *amoadd[.]w[.]aqrl'
expect_seq atomic_load '^ *lw' '^ *fence *r,rw'
expect_seq atomic_store '^ *fence *rw,w' '^ *sw'
expect_seq spin_lock 'atomic_cas>$'
expect_seq spin_unlock 'atomic_store>$'
! grep -qE '<__(atomic|sync)_[a-z_0-9]*>' "$tmp/dis"
check $? "no libatomic calls"
# pause is fence w,0, which objdump without Zihintpause shows so.
grep -qE '^ *[0-9a-f]+:[[:space:]]+fence[[:space:]]+w,unknown' "$tmp/dis"
check $? "cpu_relax is pause"

for mode in ${RISCV64_MODES:-sv48 sv39}; do
    if [ $mode = sv48 ]; then cpu=rv64; else cpu=rv64,sv48=off; fi
    echo "riscv64 $mode, three harts"
    log="$tmp/$mode.log"
    timeout 120 qemu-system-riscv64 -machine virt -cpu "$cpu" -smp 3 -accel tcg,thread=multi -m 512M \
        -bios default -kernel "$kernel" -initrd "$initrd" -append "samos.test=1" \
        -display none -monitor none -serial "file:$log.raw" < /dev/null > "$tmp/$mode-qemu.log" 2>&1 &
    pid=$!
    for _ in $(seq 1100); do
        [ -f "$log.raw" ] && tr -d '\r' < "$log.raw" | grep -aq '^SamOs v1\.0\.0 - type help' && break
        sleep 0.1
    done
    kill $pid 2> /dev/null
    wait $pid 2> /dev/null
    tr -d '\r' < "$log.raw" > "$log"
    boot=$(sed -n 's/^boot hart: \([0-9]*\)$/\1/p' "$log" | head -n 1)
    grep -aq "^mmu: $mode, " "$log"
    check $? "paging in $mode"
    for h in 0 1 2; do
        [ "$h" = "$boot" ] && continue
        grep -aEq "^smp: cpu [12] hart $h online$" "$log"
        check $? "hart $h online (boot hart $boot)"
    done
    grep -aq '^smp: 3 cpus online$' "$log"
    check $? "three cpus online"
    grep -aq '^smp selftest: call, tlb shootdown, lock and atomic counts ok$' "$log"
    check $? "SMP self-test on three harts"
    grep -aq '^SamOs v1\.0\.0 - type help' "$log"
    check $? "the shell started"
    ! grep -aEq 'did not start|KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' "$log"
    check $? "no fault"
done

qemu-system-riscv64 -machine virt,dumpdtb="$tmp/virt.dtb" -smp 4 -m 512M -display none > /dev/null 2>&1
python3 -I "$here/dtb-harts.py" "$tmp/virt.dtb" "$tmp/harts.dtb" 1 7
check $? "device tree with hart 1 disabled and a hart 7"
for mode in ${RISCV64_MODES:-sv48 sv39}; do
    if [ $mode = sv48 ]; then cpu=rv64; else cpu=rv64,sv48=off; fi
    echo "riscv64 $mode, harts 0-3 with 1 disabled, and 7"
    log="$tmp/$mode-dt.log"
    timeout 200 qemu-system-riscv64 -machine virt -cpu "$cpu" -smp 4 -accel tcg,thread=multi -m 512M \
        -bios default -dtb "$tmp/harts.dtb" -kernel "$kernel" -initrd "$initrd" \
        -append "samos.test=1 samos.run=KTEST.ELF cpubusy 6 4000" \
        -display none -monitor none -serial "file:$log.raw" < /dev/null > "$tmp/$mode-dt-qemu.log" 2>&1 &
    pid=$!
    for _ in $(seq 1900); do
        [ -f "$log.raw" ] && tr -d '\r' < "$log.raw" | grep -aE '^CPUBUSY (OK|FAIL)|KERNEL PANIC' > /dev/null && break
        sleep 0.1
    done
    kill $pid 2> /dev/null
    wait $pid 2> /dev/null
    tr -d '\r' < "$log.raw" > "$log"
    boot=$(sed -n 's/^boot hart: \([0-9]*\)$/\1/p' "$log" | head -n 1)
    grep -aE "^mmu: $mode, " "$log" > /dev/null
    check $? "paging in $mode"
    grep -aE '^smp: hart 1 skipped: disabled in the device tree$' "$log" > /dev/null
    check $? "hart 1 skipped as disabled"
    grep -aE '^smp: hart 7 did not start( \(sbi error -[0-9]+\))?$' "$log" > /dev/null
    check $? "hart 7 reported: $(grep -a '^smp: hart 7' "$log" | head -n 1)"
    slot=1
    for h in 0 2 3; do
        [ "$h" = "$boot" ] && continue
        grep -aE "^smp: cpu $slot hart $h online$" "$log" > /dev/null
        check $? "hart $h in slot $slot (boot hart $boot)"
        slot=$((slot + 1))
    done
    [ -n "$boot" ] && [ "$boot" != 1 ] && [ "$boot" != 7 ]
    check $? "boot hart $boot is cpu 0"
    grep -aE '^smp: 3 cpus online$' "$log" > /dev/null
    check $? "three cpus online"
    grep -aE '^smp selftest: call, tlb shootdown, lock and atomic counts ok$' "$log" > /dev/null
    check $? "SMP self-test on harts with gaps in their ids"
    for c in 0 1 2; do
        line=$(grep -aE "^CPUBUSY cpu $c busy [1-9][0-9]* " "$log" | head -n 1)
        [ -n "$line" ]
        check $? "cpu $c took timer ticks of its own: $line"
    done
    grep -aE '^CPUBUSY 3 cpus, children together ran on 0x7$' "$log" > /dev/null
    check $? "the children ran on all three cpus"
    grep -aE '^CPUBUSY OK$' "$log" > /dev/null
    check $? "ktest cpubusy OK"
    ! grep -aE 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' "$log" > /dev/null
    check $? "no fault"
done

# Summary line read by tests64/arch/run.sh (the arch tests' format).
echo "riscv64 arch test: $checks checks, $fails failures"
if [ $fails -eq 0 ]; then
    echo "PASS: tests64/arch/riscv64-smp"
else
    echo "FAIL: tests64/arch/riscv64-smp ($fails failed)"
    exit 1
fi
