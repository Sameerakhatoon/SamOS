#!/usr/bin/env bash
# tests64/arch/riscv64-shell.sh - an interactive shell session on
# riscv64 with its devices (QEMU virt under OpenSBI). Run after
#   bash arch-build.sh riscv64
# The initrd is "@:/" (the programs, HELLO.TXT, the font); a 16 MiB
# FAT16 image (label VDISK, with SEED.TXT) is disk 0 on virtio-blk over
# MMIO; a virtio keyboard and ramfb are plugged in; the serial console
# is a pipe this script types into.
#
# Every session runs twice: Sv48 (default CPU) and Sv39 (-cpu
# rv64,sv48=off).
#   1. devices: the virtio-blk and virtio-input slots come from the
#      device tree with their PLIC sources, the disk completes by
#      interrupt, the UART receives through the PLIC, ramfb is the
#      framebuffer.
#   2. shell over serial: file commands on the virtio disk (write,
#      cat, ls, mkdir, echo >, rm), a program with arguments.
#   3. shell over virtio-input: keys from the monitor's sendkey run
#      commands through the same shell.
#   4. the disk afterwards (mtools): the files the shell wrote are
#      there with their contents, the removed one is gone.
#   5. the screen: clear and enough help to scroll (typed over
#      serial), then a screendump of ramfb read back with the e2e
#      suite's screen_text.py shows what the shell printed for the
#      virtio-input commands on the bottom rows, below BLANK.ELF's
#      windows.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
kernel="${RISCV64_KERNEL:-$root/bin/riscv64/kernel.img}"
initrd="${RISCV64_INITRD:-$root/bin/riscv64/initrd.img}"
font="$root/assets/sysfont.bmp"
for f in "$kernel" "$initrd"; do
    [ -f "$f" ] || { echo "FAIL: $f missing; run bash arch-build.sh riscv64" >&2; exit 2; }
done
tmp="$(mktemp -d)"
[ "${RISCV64_KEEP:-0}" = 1 ] && echo "keeping $tmp" || trap 'rm -rf "$tmp"' EXIT
export MTOOLS_SKIP_CHECK=1
fails=0
checks=0

pass() { checks=$((checks + 1)); echo "  ok: $1"; }
fail() { checks=$((checks + 1)); echo "  FAIL: $1"; fails=$((fails + 1)); }
check() { if [ "$1" -eq 0 ]; then pass "$2"; else fail "$2"; fi; }
expect() { grep -aEq "$1" "$cur"; check $? "$2"; }
expect_count() {
    local n
    n=$(grep -aEc "$1" "$cur")
    [ "$n" -eq "$2" ]
    check $? "$3 (/$1/ x$n, want $2)"
}

# wait_for <regex> [count]: until the serial log has `count` (default
# 1) lines matching, or 120 s pass (a failure guard only). A prompt
# line is '^.?> ': after clear it starts with a form feed.
wait_for() {
    local left=1200
    while [ $left -gt 0 ]; do
        [ "$(tr -d '\r' < "$log.raw" | grep -aEc "$1")" -ge "${2:-1}" ] && return 0
        left=$((left - 1))
        sleep 0.1
    done
    return 1
}

# One character -> one QEMU sendkey name (what the session types).
key_for() {
    case "$1" in
        [a-z0-9]) echo "$1" ;;
        [A-Z]) echo "shift-$(echo "$1" | tr 'A-Z' 'a-z')" ;;
        ' ') echo spc ;;
        ':') echo shift-semicolon ;;
        '/') echo slash ;;
        '.') echo dot ;;
        '@') echo shift-2 ;;
    esac
}

# The monitor script (QEMU's stdin); the serial input goes to the pipe.
session() {
    local prompts=1
    exec 3> "$tmp/ser.in"
    wait_for '^SamOs v1\.0\.0 - type help' || { echo quit; return; }
    # Serial: one line, then the next prompt before the next line.
    for line in 'write 0:/NOTE.TXT typed over serial' 'cat 0:/NOTE.TXT' 'cat 0:/SEED.TXT' \
                'mkdir 0:/SUB' 'echo nested file > 0:/SUB/N.TXT' 'cat 0:/SUB/N.TXT' \
                'rm 0:/SEED.TXT' 'hello.elf one two' 'cat @:/HELLO.TXT' 'clear' 'help' 'help' 'help'; do
        wait_for '^.?> ' $prompts
        printf '%s\r' "$line" >&3
        prompts=$((prompts + 1))
    done
    # virtio-input: the same shell, keys from sendkey.
    local c line='ls 0:/'
    for line in 'ls 0:/' 'cat 0:/SUB/N.TXT'; do
        wait_for '^.?> ' $prompts
        for ((i = 0; i < ${#line}; i++)); do
            c="${line:$i:1}"
            echo "sendkey $(key_for "$c")"
            sleep 0.1
        done
        echo 'sendkey ret'
        prompts=$((prompts + 1))
    done
    wait_for '^.?> ' $prompts
    sleep 1
    echo "screendump $tmp/$mode.ppm"
    sleep 2
    echo quit
    exec 3>&-
}

for mode in ${RISCV64_MODES:-sv48 sv39}; do
    if [ $mode = sv48 ]; then cpu=rv64 levels=4; else cpu=rv64,sv48=off levels=3; fi
    echo "riscv64 $mode shell"
    disk="$tmp/$mode-vdisk.img"
    rm -f "$disk"
    mkfs.fat -C -F 16 -n VDISK "$disk" 16384 > /dev/null
    printf 'seeded by the host\n' > "$tmp/seed.txt"
    mcopy -i "$disk" "$tmp/seed.txt" ::/SEED.TXT
    log="$tmp/$mode.log"
    rm -f "$tmp/ser.in" "$tmp/ser.out"
    mkfifo "$tmp/ser.in" "$tmp/ser.out"
    cat "$tmp/ser.out" > "$log.raw" &
    reader=$!
    session | timeout 240 qemu-system-riscv64 -machine virt -cpu "$cpu" -m 512M -bios default \
        -kernel "$kernel" -initrd "$initrd" -append "samos.test=1" \
        -global virtio-mmio.force-legacy=false \
        -drive "file=$disk,format=raw,if=none,id=vd" -device virtio-blk-device,drive=vd \
        -device virtio-keyboard-device -device ramfb \
        -display none -monitor stdio -serial "pipe:$tmp/ser" > "$tmp/$mode-qemu.log" 2>&1
    wait $reader 2>/dev/null
    tr -d '\r' < "$log.raw" > "$log"
    cur="$log"

    expect "^mmu: $mode, $levels levels, root" "paging in $mode"
    expect '^virtio: [0-9]+ mmio slot\(s\) in the device tree$' "virtio-mmio slots from the device tree"
    expect '^disk: virtio-blk completions by interrupt, GSI [1-8] level active-high' "virtio-blk on its PLIC source"
    expect '^disk 0 real VIRTIO-BLK fs=FAT label=VDISK +rw$' "the virtio disk mounted as disk 0"
    expect '^disk 1 real RAMDISK fs=FAT label=SAMOS +rw$' "the initrd still mounted as the RAM disk"
    expect '^virtio-input: QEMU Virtio Keyboard, GSI [1-8], ' "virtio-input on its PLIC source"
    expect '^uart: keys from plic irq 10$' "the UART's receive interrupt through the PLIC"
    expect '^ramfb: 1024x768 at 0x' "ramfb set up through fw_cfg"
    expect '^graphics: framebuffer 1024x768 pitch 1024' "boot_info's framebuffer reached graphics"
    expect_count 'polled|timed out|interrupt missing' 0 "no disk fallback to polling"

    expect '^typed over serial$' "serial: write then cat on the virtio disk"
    expect '^seeded by the host$' "serial: cat of the host's file"
    expect '^nested file$' "serial: mkdir, echo > and cat in a subdirectory"
    expect '^hello: argc=3$' "serial: a program with arguments"
    expect 'virtio-input: first key event$' "virtio-input: keys arrived"
    expect '^(> )?ls 0:/$' "virtio-input: the typed command echoed"
    expect '^ *NOTE\.TXT' "virtio-input: ls lists the shell's file"
    expect_count '^nested file$' 2 "virtio-input: cat ran (second copy)"
    expect_count 'KERNEL FAULT|KERNEL PANIC|USER FAULT|command not found' 0 "no fault, every command known"

    [ "$(mtype -i "$disk" ::/NOTE.TXT 2>/dev/null)" = "typed over serial" ]
    check $? "disk: NOTE.TXT holds what the shell wrote"
    [ "$(mtype -i "$disk" ::/SUB/N.TXT 2>/dev/null)" = "nested file" ]
    check $? "disk: SUB/N.TXT holds what echo > wrote"
    ! mdir -i "$disk" ::/SEED.TXT > /dev/null 2>&1
    check $? "disk: SEED.TXT removed"
    fsck.fat -n "$disk" > "$tmp/fsck.log" 2>&1
    check $? "disk: fsck.fat finds the volume clean"

    if [ -s "$tmp/$mode.ppm" ]; then
        python3 -I "$root/tests64/e2e/screen_text.py" "$tmp/$mode.ppm" "$font" > "$tmp/$mode.screen"
        grep -q '^> ls 0:/$' "$tmp/$mode.screen"
        check $? "screen: the virtio-input command on ramfb"
        grep -q '^NOTE.TXT  *18$' "$tmp/$mode.screen"
        check $? "screen: its output (ls lists NOTE.TXT, 18 bytes)"
        grep -q '^nested file$' "$tmp/$mode.screen"
        check $? "screen: the cat output"
    else
        fail "screen: no screendump"
        fail "screen: no screendump"
    fi
done

# Summary line read by tests64/arch/run.sh (the arch tests' format).
echo "riscv64 arch test: $checks checks, $fails failures"
if [ $fails -eq 0 ]; then
    echo "PASS: tests64/arch/riscv64-shell"
else
    echo "FAIL: tests64/arch/riscv64-shell ($fails failed)"
    exit 1
fi
