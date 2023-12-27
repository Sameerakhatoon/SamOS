#!/usr/bin/env bash
# aarch64 exceptions, GICv3, generic timer and preemption.
#
# Builds bin/aarch64/kernel.img and boots it on -machine
# virt,gic-version=3 -cpu cortex-a72 -m 512M:
#   brk, undef, dabort: samos.fault_test=<kind> takes a BRK, a UDF and
#          a data abort (load from the end of RAM, 0x60000000, through
#          its Device mapping at 0xffff800060000000 since the aarch64 MMU work). Each
#          dump must decode ESR_EL1, show the marker registers x2/x28
#          and have ELR at the faulting instruction, whose address
#          comes from nm on kernel.elf (fault_test_*_insn).
#   svc:   an SVC that returns: ELR is the instruction after it, x0 is
#          changed in the frame and the change survives the restore.
#   tick:  GICv3 found in the DT, this CPU's redistributor, the timer
#          PPI from the DT, 100 Hz; ticks match CNTPCT_EL0 and the idle
#          wfi sleeps between ticks (at most 2 wakeups per tick; fewer
#          on a stalled host). Also with -smp 4 (secondaries stay
#          in PSCI; the redistributor walk sees 4 frames).
#   sched: samos.sched_test: two busy kernel threads that never yield
#          interleave their lines, so the tick preempts them; their
#          register checksums survive; then 100 idle ticks against
#          CNTPCT_EL0 (within 3 ticks) and wfi sleeping between ticks. Also
#          entered at EL2 (virtualization=on).
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
timeout_s="${ARCH_TIMEOUT:-20}"
cross="${CROSS:-aarch64-linux-gnu-}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

checks=0
failures=0

bash "$root/arch-build.sh" aarch64 > "$tmp/build.log" 2>&1 || {
    cat "$tmp/build.log"
    echo "FAIL: aarch64 build"
    echo "aarch64 arch test: 1 checks, 1 failures"
    exit 1
}

# Address of a kernel symbol as the dump prints it (0x..., no padding).
sym() {
    local a
    a="$("${cross}nm" "$root/build/aarch64/kernel.elf" | awk -v s="$1" '$3 == s { print $1 }')"
    [ -n "$a" ] && printf '0x%x' "$((16#$a + ${2:-0}))"
}

# boot <name> <machine> <smp> <append> <last line ERE>
boot() {
    local name="$1" machine="$2" smp="$3" append="$4" done_re="$5"
    local log="$tmp/$name.log"
    qemu-system-aarch64 -machine "$machine" -cpu cortex-a72 -m 512M -smp "$smp" -nographic \
        -kernel "$root/bin/aarch64/kernel.img" -append "$append" \
        < /dev/null > "$log" 2>&1 &
    local pid=$! waited=0
    while kill -0 "$pid" 2>/dev/null; do
        if grep -aqE "$done_re" "$log"; then
            break
        fi
        if [ $waited -ge $((timeout_s * 10)) ]; then
            echo "  ($name: timed out after ${timeout_s}s)"
            break
        fi
        sleep 0.1
        waited=$((waited + 1))
    done
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    tr -d '\r' < "$log" > "$log.txt"
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

# expect <name> <ERE> <description>
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

# expect_range <name> <ERE with one ([0-9]+) group> <lo> <hi> <description>
expect_range() {
    local v
    v="$(sed -nE "s/$2/\\1/p" "$tmp/$1.log.txt" | head -1)"
    [ -n "$v" ] && [ "$v" -ge "$3" ] && [ "$v" -le "$4" ]
    result $? "$1: $5 (${v:-none} in $3..$4)"
}

# Ticks counted by the IRQ against ticks the counter says elapsed.
expect_ticks_match() {
    local line t c
    line="$(grep -aE '^timer: [0-9]+ ticks since start, CNTPCT says [0-9]+$' "$tmp/$1.log.txt" | head -1)"
    t="$(echo "$line" | sed -nE 's/^timer: ([0-9]+) ticks.*/\1/p')"
    c="$(echo "$line" | sed -nE 's/.*CNTPCT says ([0-9]+)$/\1/p')"
    [ -n "$t" ] && [ -n "$c" ] && [ $((t > c ? t - c : c - t)) -le 2 ]
    result $? "$1: tick count matches CNTPCT_EL0 since start (${t:-?} vs ${c:-?}, within 2)"
}

dump() {
    echo "---- $1 serial ----"
    cat "$tmp/$1.log.txt"
}

gic3=virt,gic-version=3

# ---- synchronous exceptions ----------------------------------------
for kind in brk undef dabort svc; do
    insn="$(sym fault_test_${kind}_insn)"
    if [ -z "$insn" ]; then
        result 1 "nm: fault_test_${kind}_insn in kernel.elf"
    fi
    if [ $kind = svc ]; then
        boot $kind $gic3 1 "samos.fault_test=$kind" '^SamOS aarch64: hello'
    else
        boot $kind $gic3 1 "samos.fault_test=$kind" '^exception: halted'
    fi
    dump $kind
    expect $kind "^fault test: $kind\$" "test hook ran"
    case $kind in
    brk)
        expect brk '^  ESR 0xf200005a \(EC 0x3c, brk\)$' "ESR decoded as BRK"
        expect brk '^  cause: brk #0x5a$' "BRK immediate"
        ;;
    undef)
        expect undef '^  ESR 0x2000000 \(EC 0x0, undefined instruction\)$' "ESR decoded as undefined"
        expect undef '^  cause: undefined instruction 0x0000002a$' "the UDF encoding at ELR"
        ;;
    dabort)
        expect dabort '^  ESR 0x96000010 \(EC 0x25, data abort\)$' "ESR decoded as data abort at EL1"
        expect dabort '^  cause: data abort, read, DFSC 0x10 \(synchronous external abort\), FAR 0xffff800060000000$' "DFSC and direction"
        expect dabort '^  FAR 0xffff800060000000$' "FAR is the address loaded"
        expect dabort '^  x0  0xffff800060000000 ' "x0 holds the address in the dump"
        ;;
    svc)
        svc_next="$(sym fault_test_svc_insn 4)"
        expect svc "^svc: #0x80 at EL1, ELR $svc_next, x0 0x5a, x2 0x2222\$" "ELR after the svc ($svc_next), frame registers"
        expect svc '^fault test: svc returned 0x5b$' "x0 changed in the frame is restored"
        expect svc '^SamOS aarch64: hello$' "boot continues after the svc"
        expect_no svc '^exception:' "no exception dump"
        ;;
    esac
    if [ $kind != svc ]; then
        expect $kind '^exception: sync \(current EL, SP_ELx\)$' "sync exception at EL1h"
        expect $kind "^  ELR $insn\$" "ELR is fault_test_${kind}_insn ($insn)"
        expect $kind '  x2  0x0000000000002222( |$)' "x2 marker in the dump"
        expect $kind '  x28 0x0000000000002828( |$)' "x28 marker in the dump"
        expect $kind '^  sp  0xffffffff800[0-9a-f]{5}$' "sp in the kernel stack"
        expect $kind '^exception: halted$' "halted"
        expect_no $kind '^SamOS aarch64: hello$' "no hello after the fault"
    fi
done

# ---- GICv3 and the tick --------------------------------------------
for smp in 1 4; do
    name=tick$smp
    boot $name $gic3 $smp "samos.test=1" '^SamOS aarch64: hello'
    dump $name
    expect $name '^gic: GICv3 distributor at 0x8000000, [0-9]+ INTIDs$' "distributor from the DT"
    expect $name "^gic: redistributor at 0x80a0000 \\(frame 0 of $smp, affinity 0x0\\)\$" "this CPU's redistributor among $smp"
    expect $name '^timer: [1-9][0-9]+ Hz counter \(CNTFRQ_EL0, (no DT clock-frequency|DT clock-frequency agrees)\), EL1 physical timer INTID 30 \(DT interrupts\)$' "counter frequency, timer PPI from the DT"
    expect $name '^timer: 100 Hz tick, period [0-9]+ counts$' "100 Hz"
    expect_range $name '^timer: 10 ticks took ([0-9]+) us by CNTPCT$' 70000 130000 "10 ticks against CNTPCT_EL0"
    # Upper bound only: a busy idle loop wakes far more often than it
    # ticks. Fewer wakeups than ticks is fine: when the host stalls the
    # vCPU past a period, the re-armed deadline fires again before the
    # idle loop counts, so one wakeup covers two ticks.
    expect_range $name '^idle: ([0-9]+) wfi wakeups in 10 ticks$' 1 20 "idle wfi sleeps until each tick"
    expect_ticks_match $name
    expect $name '^SamOS aarch64: hello$' "hello"
    expect_no $name '^(exception|irq):' "no exception or stray IRQ"
done

# ---- preemption ----------------------------------------------------
# Interleaving: the thread letter changes between consecutive lines.
expect_interleaved() {
    local seq alternations
    seq="$(grep -aoE '^thread [AB]: line [0-9]+' "$tmp/$1.log.txt" | cut -c8 | tr -d '\n')"
    alternations="$(echo "$seq" | fold -w1 | uniq | wc -l)"
    [ "${#seq}" -eq 16 ] && [ "$alternations" -ge 8 ]
    result $? "$1: threads A and B interleave (${seq:-none}, $alternations runs)"
}

expect_in_order() {
    local got
    got="$(grep -aoE "^thread $2: line [0-9]+" "$tmp/$1.log.txt" | sed 's/.* //' | tr '\n' ' ')"
    [ "$got" = "0 1 2 3 4 5 6 7 " ]
    result $? "$1: thread $2 printed lines 0..7 in order (${got:-none})"
}

for mode in el1 el2; do
    name=sched_$mode
    machine=$gic3
    [ $mode = el2 ] && machine=$gic3,virtualization=on
    boot $name $machine 1 "samos.sched_test" '^SamOS aarch64: hello'
    dump $name
    expect $name "^SamOS aarch64: booting \\(entered at EL${mode#el}, running at EL1\\)\$" "entered at EL${mode#el}"
    expect $name '^sched: 2 threads spawned$' "two kernel threads"
    expect_in_order $name A
    expect_in_order $name B
    expect_interleaved $name
    expect $name '^thread A: done, [0-9]+ loops, checksum ok$' "thread A registers survive preemption"
    expect $name '^thread B: done, [0-9]+ loops, checksum ok$' "thread B registers survive preemption"
    expect_range $name '^sched: threads done, ([0-9]+) switches, now on thread 0$' 8 1000 "timer switches, back on idle"
    expect_range $name '^timer: 100 ticks took ([0-9]+) us by CNTPCT$' 970000 1030000 "100 ticks against CNTPCT_EL0 (within 3 ticks)"
    expect_range $name '^idle: ([0-9]+) wfi wakeups in 100 ticks$' 1 200 "idle wfi sleeps until each tick (see tick test)"
    expect_ticks_match $name
    expect $name '^SamOS aarch64: hello$' "hello"
    expect_no $name '^(exception|irq|trap):' "no exception or stray IRQ"
done

echo "aarch64 arch test: $checks checks, $failures failures"
[ $failures -eq 0 ]
