#!/usr/bin/env bash
# aarch64 MMU and memory.
#
# Builds bin/aarch64/kernel.img and boots it on -machine
# virt,gic-version=3 -cpu cortex-a72:
#   layout: -m 512M samos.mmu_test. The MMU line (4 KiB, 48-bit VA, 4
#           levels, SCTLR M C I WXN, MAIR), the kernel at
#           0xffffffff80000000, the identity map freed. AT S1E1R/W
#           probes: text R-X, rodata R--, data/bss/stack RW-, the stack
#           guard and the old identity address fault, the direct map
#           and the DTB RW- Normal (attr 0xff), the UART Device (0x04).
#           The PFA: frames = RAM / 4 KiB, reserved = the memmap
#           reservations + its table; the PFA test takes every free
#           frame (none reserved, all writable through the direct map),
#           frees them all; user spaces: two ASIDs, the same VA to two
#           frames, EL0 permissions by AT S1E0R/W, PXN on user pages,
#           tables freed. Then GICv3, timer and hello with the MMU on.
#   el2:    the same entered at EL2.
#   m128, m2g: -m 128M and -m 2G: frames, reservations, PFA test.
#   reserve: -dtb with a /memreserve/ entry and /reserved-memory
#           (dtb-reserve.py): each region in the memmap, no-map ones
#           missing from the direct map, the dynamic one listed, the
#           disabled one ignored, none of them allocated.
#   textwrite, dataexec, ident, wxn, uexec: samos.fault_test=<kind>,
#           a store to kernel text, a call into .data, a load from the
#           removed identity map, a call into a writable page with PXN
#           clear (WXN), a call into a user page (PXN): each a data or
#           instruction abort with the right ESR, FAR and ELR (nm).
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
timeout_s="${ARCH_TIMEOUT:-30}"
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

# boot <name> <machine> <mem> <append> <last line ERE> [extra qemu args]
boot() {
    local name="$1" machine="$2" mem="$3" append="$4" done_re="$5"
    shift 5
    local log="$tmp/$name.log"
    qemu-system-aarch64 -machine "$machine" -cpu cortex-a72 -m "$mem" -nographic \
        -kernel "$root/bin/aarch64/kernel.img" -append "$append" "$@" \
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

dump() {
    echo "---- $1 serial ----"
    cat "$tmp/$1.log.txt"
}

# The PFA's frame count is RAM / 4 KiB, and its reserved count is the
# memmap's reservations (each rounded out to frames) plus its table.
expect_pfa_counts() {
    local name="$1" mib="$2" line frames reserved table want=0 a b
    line="$(grep -aE '^pfa: [0-9]+ frames' "$tmp/$name.log.txt" | head -1)"
    frames="$(echo "$line" | sed -nE 's/^pfa: ([0-9]+) frames.*/\1/p')"
    reserved="$(echo "$line" | sed -nE 's/.* ([0-9]+) reserved.*/\1/p')"
    table="$(echo "$line" | sed -nE 's/.*\(([0-9]+) bytes\)$/\1/p')"
    [ "${frames:-0}" -eq $((mib * 256)) ]
    result $? "$name: PFA spans ${frames:-?} frames ($mib MiB / 4 KiB)"
    while read -r a b; do
        want=$((want + ((16#${b#0x} + 4095) / 4096) - (16#${a#0x} / 4096)))
    done < <(grep -aE '^memmap: 0x[0-9a-f]+-0x[0-9a-f]+ ' "$tmp/$name.log.txt" | grep -v ' memory$' |
             sed -nE 's/^memmap: (0x[0-9a-f]+)-(0x[0-9a-f]+) .*/\1 \2/p')
    want=$((want + ${table:-0} / 4096))
    [ -n "$reserved" ] && [ "$reserved" -eq "$want" ]
    result $? "$name: reserved frames = memmap reservations + PFA table (${reserved:-?} vs $want)"
}

# The PFA test: every free frame allocated, none reserved, all mapped,
# all freed again.
expect_pfa_test() {
    local name="$1" n
    n="$(sed -nE 's/^pfa test: allocated ([0-9]+) of \1 free frames in [0-9]+ runs \(0 not recorded\)$/\1/p' \
        "$tmp/$name.log.txt")"
    [ -n "$n" ] && [ "$n" -gt 1000 ]
    result $? "$name: PFA test allocated all ${n:-?} free frames"
    expect "$name" '^pfa test: 0 in reserved memory, 0 not writable through the direct map$' \
        "no allocated frame reserved, every one writable through the direct map"
    expect "$name" "^pfa test: 0 frees refused, ${n:-x} free again\$" "all freed, free count restored"
    expect "$name" '^pfa test: free 0, double free -1, free of the kernel -1, free of the table -1$' \
        "double free and frees of reserved frames refused"
}

gic3=virt,gic-version=3

# ---- layout, PFA, address spaces -----------------------------------
for name in layout el2; do
    machine=$gic3
    [ $name = el2 ] && machine=$gic3,virtualization=on
    boot $name $machine 512M "samos.mmu_test" '^SamOS aarch64: hello|^exception: halted'
    dump $name
    expect $name '^mmu: 4 KiB granule, 48-bit VA, 4 levels, [0-9]+-bit PA, 8-bit ASIDs$' "4 KiB, 48-bit VA, 4 levels"
    expect $name '^mmu: MAIR 0x4404ff \(attr0 normal WB, attr1 device nGnRE, attr2 normal NC\), SCTLR M C I WXN$' \
        "MAIR normal, device and normal non-cacheable, MMU and caches on, WXN"
    expect $name '^mmu: kernel 0xffffffff80000000-0xffffffff80[0-9a-f]{6} at pa 0x40200000 ' \
        "kernel linked high, mapped to its load address"
    expect $name '^mmu: direct map at 0xffff800000000000, [0-9]+ table frames, identity map removed \([1-9][0-9]* tables freed\)$' \
        "identity map tables freed"
    expect $name "^probe: text $(sym probe): pa 0x402[0-9a-f]{5}, L3, R-X, attr 0xff\$" "text RX (pages)"
    expect $name "^probe: rodata $(sym _text_end): pa 0x402[0-9a-f]{5}, L3, R--, attr 0xff\$" "rodata R, XN"
    expect $name "^probe: data $(sym data_ret): pa 0x402[0-9a-f]{5}, L3, RW-, attr 0xff\$" "data RW, XN"
    expect $name "^probe: bss $(sym bss_word): pa 0x402[0-9a-f]{5}, L3, RW-, attr 0xff\$" "bss RW, XN"
    expect $name '^probe: stack 0xffffffff80[0-9a-f]{6}: pa 0x402[0-9a-f]{5}, L3, RW-, attr 0xff$' "stack RW, XN"
    expect $name "^probe: guard $(sym _stack_guard): fault, FST 0x7\$" "stack guard page unmapped"
    expect $name '^probe: directmap 0xffff8000[0-9a-f]{8}: pa 0x[0-9a-f]{8}, L2, RW-, attr 0xff$' \
        "direct map RW, XN, Normal, 2 MiB blocks"
    expect $name '^probe: dtb 0xffff800048000000: pa 0x48000000, L2, RW-, attr 0xff$' "DTB through the direct map"
    expect $name '^probe: uart 0xffff800009000000: pa 0x9000000, L3, RW-, attr 0x4$' "UART Device-nGnRE, XN, high"
    expect $name '^probe: identity 0x40200000: fault, FST 0x4$' "old identity address: level 0 translation fault"
    expect_pfa_counts $name 512
    expect_pfa_test $name
    expect $name '^aspace: A asid 1, B asid 2, 0x400000 -> 0x[0-9a-f]+ in A, 0x[0-9a-f]+ in B$' "two spaces, two ASIDs"
    expect $name '^aspace: map of a kernel address refused: -1$' "no user mapping in the kernel half"
    expect $name '^aspace: A user page descriptor nG 1 AP\[1\] 1 AP\[2\] 0 PXN 1 UXN 1$' "user RW page: nG, EL0, PXN, UXN"
    expect $name '^aspace: A reads 0xaaaaaaaa through 0x400000$' "A's frame through TTBR0"
    expect $name '^aspace: A EL0 read 0x400000: ok, pa ' "AT S1E0R on A's page"
    a_pa="$(sed -nE 's/^aspace: A asid 1, B asid 2, 0x400000 -> (0x[0-9a-f]+) in A.*/\1/p' "$tmp/$name.log.txt")"
    expect $name "^aspace: A EL0 read 0x400000: ok, pa ${a_pa:-none}\$" "AT S1E0R translates to A's frame"
    expect $name '^aspace: A EL0 write 0x401000 \(read-only page\): permission fault$' "EL0 write to a read-only page"
    expect $name '^aspace: A EL0 read of kernel text: permission fault$' "EL0 cannot read the kernel"
    expect $name '^aspace: B reads 0xbbbbbbbb through 0x400000$' "same VA, B's frame after the switch"
    expect $name '^aspace: B wrote through 0x400000, frame holds 0xb0b0b0b0$' "EL1 write through TTBR0 lands in B's frame"
    expect $name '^aspace: back on A, reads 0xaaaaaaaa$' "ASIDs keep the spaces apart without a flush"
    expect $name '^aspace: no user space, EL1 read 0x400000: translation fault$' "empty TTBR0"
    expect $name '^aspace: destroyed, ([0-9]+) free frames \(was \1\), ([0-9]+) table frames \(was \2\)$' \
        "destroy frees every table and frame"
    expect $name '^gic: GICv3 distributor at 0x8000000, [0-9]+ INTIDs$' "GIC through its Device mapping"
    expect $name '^timer: 100 Hz tick, period [0-9]+ counts$' "tick with the MMU on"
    expect $name '^SamOS aarch64: hello$' "hello"
    expect_no $name '^(exception|irq|trap|mmu: .*halted)' "no exception"
done
expect el2 '^SamOS aarch64: booting \(entered at EL2, running at EL1\)$' "entered at EL2"

# ---- other RAM sizes -----------------------------------------------
for mem in 128 2048; do
    name=m$mem
    timeout_s=$((${ARCH_TIMEOUT:-30} * (mem / 512 + 1)))
    boot $name $gic3 ${mem}M "samos.mmu_test" '^SamOS aarch64: hello|^exception: halted'
    dump $name
    expect $name "^memory: $mem MiB in 1 region\\(s\\)\$" "$mem MiB from /memory"
    expect_pfa_counts $name $mem
    expect_pfa_test $name
    expect $name '^aspace: destroyed, ([0-9]+) free frames \(was \1\)' "user spaces leak nothing"
    expect $name '^SamOS aarch64: hello$' "hello"
    expect_no $name '^(exception|irq|trap)' "no exception"
done
timeout_s="${ARCH_TIMEOUT:-30}"

# ---- reservations from the device tree -----------------------------
qemu-system-aarch64 -machine $gic3,dumpdtb="$tmp/virt.dtb" -cpu cortex-a72 -m 512M -nographic > /dev/null 2>&1
if python3 "$here/dtb-reserve.py" "$tmp/virt.dtb" "$tmp/reserve.dtb"; then
    boot reserve $gic3 512M "samos.mmu_test" '^SamOS aarch64: hello|^exception: halted' -dtb "$tmp/reserve.dtb"
    dump reserve
    expect reserve '^memmap: 0x4a000000-0x4a010000 memreserve$' "/memreserve/ entry"
    expect reserve '^memmap: 0x4c000000-0x4c200000 reserved-memory fw@4c000000 \(no-map\)$' "no-map region"
    expect reserve '^memmap: 0x4c400000-0x4c501000 reserved-memory shm@4c400000$' "mapped reserved region"
    expect reserve '^memmap: 0x4d001000-0x4d004000 reserved-memory scratch@4d001000 \(no-map\)$' "unaligned no-map region"
    expect reserve '^memmap: reserved-memory cma: dynamic, 0x1000000 bytes, not placed$' "dynamic region listed"
    expect_no reserve 'off@4e000000' "disabled region ignored"
    expect reserve '^probe: nomap 0xffff80004c000000: fault, FST 0x6$' "no-map block missing from the direct map"
    expect reserve '^probe: nomap 0xffff80004d001000: fault, FST 0x7$' "no-map pages missing from the direct map"
    expect_pfa_counts reserve 512
    expect_pfa_test reserve
    expect reserve '^SamOS aarch64: hello$' "hello"
    expect_no reserve '^(exception|irq|trap)' "no exception"
else
    result 1 "reserve: dtb-reserve.py on QEMU's tree"
fi

# ---- aborts --------------------------------------------------------
# fault <kind> <ESR> <cause ERE> <FAR> <ELR>
fault() {
    local kind="$1" esr="$2" cause="$3" far="$4" elr="$5"
    boot $kind $gic3 512M "samos.fault_test=$kind" '^exception: halted'
    dump $kind
    expect $kind "^  ESR $esr " "ESR $esr"
    expect $kind "^  cause: $cause, FAR $far\$" "cause and FAR"
    expect $kind "^  FAR $far\$" "FAR $far"
    expect $kind "^  ELR $elr\$" "ELR $elr"
    expect $kind '^exception: halted$' "halted"
    expect_no $kind '^SamOS aarch64: hello$' "no hello after the fault"
}

text="$(sym fault_test_brk)"
fault textwrite 0x9600004f 'data abort, write, DFSC 0xf \(permission fault\)' "$text" "$(sym fault_test_store_insn)"
expect textwrite "^fault test: store to kernel text $text\$" "store target is kernel text"
data="$(sym data_ret)"
fault dataexec 0x8600000f 'instruction abort, IFSC 0xf \(permission fault\)' "$data" "$data"
fault ident 0x96000004 'data abort, read, DFSC 0x4 \(translation fault\)' 0x40200000 "$(sym fault_test_dabort_insn)"
fault wxn 0x8600000f 'instruction abort, IFSC 0xf \(permission fault\)' 0xffffffffc0000000 0xffffffffc0000000
expect wxn '^fault test: call into a W\+X page 0xffffffffc0000000 \(AP\[2\] 0, PXN 0\)$' "page writable, PXN clear: only WXN"
fault uexec 0x8600000f 'instruction abort, IFSC 0xf \(permission fault\)' 0x400000 0x400000
expect uexec '^fault test: call into a user page 0x400000 \(UXN 0, PXN 1\)$' "EL0 executable, PXN set"

echo "aarch64 arch test: $checks checks, $failures failures"
[ $failures -eq 0 ]
