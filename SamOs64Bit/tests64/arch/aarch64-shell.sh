#!/usr/bin/env bash
# The interactive shell on aarch64 with its devices, typed over
# the serial line (the PL011's receive interrupt, arch.c):
#
#   QEMU virt (GICv3, Cortex-A72), bin/aarch64/os.img on
#   virtio-blk-device (virtio-mmio), a second virtio-blk-device with a
#   FAT32 volume, -device ramfb, -nographic: the serial line and the
#   monitor share stdio (Ctrl-A c switches).
#
#   serial: the boot sets up the disks (completions by interrupt), the
#           screen (ramfb) and the UART's receive line; BLANK.ELF hands
#           over to the shell, which reads every key from the serial
#           line: echo, a DEL that erases, ls, and file commands on
#           both virtio disks (write, echo >>, cat, mkdir, ls, rm,
#           rmdir), a program run with arguments.
#   screen: a monitor screendump of the ramfb, read back with
#           e2e/screen_text.py: the shell's lines are on the screen.
#   host:   mtools reads the file SamOS wrote on the FAT32 disk.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
timeout_s="${ARCH_TIMEOUT:-180}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
export MTOOLS_SKIP_CHECK=1

checks=0
failures=0
result() {
    checks=$((checks + 1))
    if [ "$1" -eq 0 ]; then echo "PASS: $2"; else failures=$((failures + 1)); echo "FAIL: $2"; fi
}

bash "$root/arch-build.sh" aarch64 > "$tmp/build.log" 2>&1 || {
    cat "$tmp/build.log"
    echo "FAIL: aarch64 build"
    echo "aarch64 arch test: 1 checks, 1 failures"
    exit 1
}
img="$root/bin/aarch64/os.img"
log="$tmp/serial.log"
screen="$tmp/screen.ppm"
truncate -s 64M "$tmp/data.img"
mkfs.fat -F 32 -s 1 -n DATAVOL "$tmp/data.img" > /dev/null

# wait_for <fixed string> <seconds> [from byte]
wait_for() {
    local t=0
    while [ $t -lt $(($2 * 10)) ]; do
        tail -c +"$((${3:-0} + 1))" "$log" 2>/dev/null | tr -d '\r' | grep -aF -- "$1" > /dev/null && return 0
        sleep 0.1
        t=$((t + 1))
    done
    echo "  (timed out waiting for '$1')" >&2
    return 1
}

# cmd <line>: type it over the serial line, Enter as CR, and wait for
# the next prompt ("> " at the start of a line).
cmd() {
    local from t=0
    from=$(stat -c %s "$log" 2>/dev/null || echo 0)
    printf '%s\r' "$1"
    while [ $t -lt 600 ]; do
        tail -c +"$((from + 1))" "$log" | tr -d '\r' | sed '1d' | grep -a '^> ' > /dev/null && return 0
        sleep 0.1
        t=$((t + 1))
    done
    echo "  (no prompt after '$1')" >&2
}

feed() {
    wait_for 'e2e: user selftest done' "$timeout_s" || return
    sleep 1
    printf '\r'                                 # ends BLANK.ELF's key probe
    wait_for 'SamOs v1.0.0 - type help' 60 || return
    sleep 1
    cmd 'echo typed over serial'
    printf 'echo abX\177c\r'                    # DEL erases the X
    sleep 2
    cmd 'ls @:/'
    cmd 'write @:/SER.TXT first line'
    cmd 'echo second line >> @:/SER.TXT'
    cmd 'cat @:/SER.TXT'
    cmd 'rm @:/SER.TXT'
    cmd 'cat @:/SER.TXT'
    cmd 'mkdir 1:/SERDIR'
    cmd 'write 1:/SERDIR/NOTE.TXT on the second virtio disk'
    cmd 'ls 1:/SERDIR'
    cmd 'mkdir 1:/GONE'
    cmd 'rmdir 1:/GONE'
    cmd 'hello.elf one two'
    cmd 'echo serial-done'
    sleep 2
    printf '\001c'                              # to the monitor
    sleep 1
    printf 'screendump %s\n' "$screen"
    sleep 3
    printf 'quit\n'
    sleep 2
}

feed | timeout $((timeout_s + 300)) qemu-system-aarch64 -machine virt,gic-version=3 -cpu cortex-a72 -m 512M \
    -nographic -global virtio-mmio.force-legacy=false -kernel "$root/bin/aarch64/kernel.img" \
    -drive "file=$img,format=raw,if=none,id=os,snapshot=on" -device virtio-blk-device,drive=os \
    -drive "file=$tmp/data.img,format=raw,if=none,id=data" -device virtio-blk-device,drive=data \
    -device ramfb > "$log" 2>&1
tr -d '\r' < "$log" > "$log.txt"

expect() {
    grep -aqE -- "$1" "$log.txt"
    result $? "serial: $2"
}
expect '^virtio-blk: disk 0, 700 MiB, flush, queue [0-9]+$' "boot disk on virtio-blk over virtio-mmio"
expect '^virtio-blk: disk 1, 64 MiB, flush, queue [0-9]+$' "second virtio-blk disk"
[ "$(grep -acE '^disk: virtio-blk completions by interrupt, GSI [0-9]+ edge active-high, vector [0-9]+$' "$log.txt")" -eq 2 ]
result $? "both disks complete by interrupt (GIC SPI, edge)"
expect '^ramfb: 1024x768 XRGB8888 framebuffer at pa 0x[0-9a-f]+$' "ramfb set up through fw_cfg"
expect '^graphics: framebuffer format 0 \(BGRX\)$' "boot_info hands graphics_setup a real framebuffer"
expect '^console: PL011 receive on INTID [0-9]+, bytes are keys$' "PL011 receive interrupt"
expect '^typed over serial$' "a line typed over serial runs"
expect '^abc$' "DEL is Backspace"
expect '^SHELL\.ELF +[0-9]+$' "ls @:/ on the boot disk"
expect '^first line$' "write then cat on the boot disk"
expect '^second line$' "echo >> appended"
expect '^cat: @:/SER\.TXT: no such file or directory$' "rm removed it"
expect '^NOTE\.TXT +26$' "mkdir, write, ls on the second disk"
expect '(^|!)hello: argc=3$' "a program run from the shell with arguments"
expect '^serial-done$' "the session finished"
grep -aqE '^(mkdir|write|echo|rm|rmdir|ls): ' "$log.txt"
[ $? -ne 0 ]
result $? "no file command failed"
grep -aqE 'KERNEL PANIC|KERNEL FAULT|USER FAULT|^exception:|^irq: unexpected|request timed out|completion interrupt missing' "$log.txt"
[ $? -ne 0 ]
result $? "no fault, stray IRQ or polling fallback"

[ "$(mtype -i "$tmp/data.img" ::SERDIR/NOTE.TXT 2>/dev/null)" = "on the second virtio disk" ]
result $? "host: mtools reads the file SamOS wrote"

if [ -s "$screen" ]; then
    python3 "$root/tests64/e2e/screen_text.py" "$screen" "$root/assets/sysfont.bmp" > "$tmp/screen.txt" 2>&1
    grep -q 'typed over serial' "$tmp/screen.txt"
    result $? "screen: the shell's output is on the ramfb (screendump)"
else
    result 1 "screen: no screendump"
fi

if [ $failures -gt 0 ]; then
    echo "---- serial ----"
    tail -n 80 "$log.txt"
    [ -f "$tmp/screen.txt" ] && { echo "---- screen ----"; cat "$tmp/screen.txt"; }
fi
echo "aarch64 arch test: $checks checks, $failures failures"
[ $failures -eq 0 ]
