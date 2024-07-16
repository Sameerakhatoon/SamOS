#!/usr/bin/env bash
# SamCompiler-built HELLO, shell builtins and shell on aarch64;
# see samcc.inc.
arch=aarch64
# boot_samos <append> <ERE>...: QEMU until every ERE is in the output,
# or 300 s pass (a loaded host is slow); the raw output is $tmp/boot.raw.
boot_samos() {
    local append="$1" left=3000 done re
    shift
    : > "$tmp/boot.raw"
    qemu-system-aarch64 -machine virt,gic-version=3 -cpu cortex-a72 -m 512M -nographic \
        -kernel "$root/bin/aarch64/kernel.img" -initrd "$tmp/initrd.img" -append "$append" \
        < /dev/null > "$tmp/boot.raw" 2>&1 &
    local pid=$!
    while [ $left -gt 0 ] && kill -0 $pid 2>/dev/null; do
        done=1
        for re in "$@"; do
            tr -d '\r' < "$tmp/boot.raw" | grep -aE "$re" > /dev/null || { done=0; break; }
        done
        [ $done -eq 1 ] && break
        left=$((left - 1))
        sleep 0.1
    done
    sleep 0.5
    kill $pid 2>/dev/null
    wait $pid 2>/dev/null
}
. "$(dirname "$0")/samcc.inc"
