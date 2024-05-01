#!/usr/bin/env bash
# Shared body of e2e/183-186: disk completions arrive as
# interrupts through the IOAPIC (or, with samos.disk_irq_masked, the
# line stays masked and the driver falls back to polling). Sourced
# after _lib.sh and _shell_lib.sh with E2E_MACHINE set. Not a test.
#
# The proof, per machine:
#   - serial: the driver reports its GSI, trigger, polarity, vector;
#   - boot markers 185 (AHCI) / 186 (PATA): completions the wait took
#     at the interrupt, dumped after the boot and again after a shell
#     write and read, so they must grow; 187 (polled) stays unset;
#   - QEMU `info pic`: the IOAPIC entry for that GSI has the disk
#     vector, the trigger mode, and is not masked;
#   - QEMU `info irq`: the IOAPIC saw that GSI assert.
# Masked: the same report says "line masked", the driver gives up on
# the interrupt after 3 completions (187 = 3), the boot and the shell
# write still work, and the device still asserted the line.

# diskirq_check <irq|masked>
diskirq_check() {
    local mode=$1 m=$E2E_MACHINE name gsi trig vec marker
    if [ "$m" = q35 ]; then
        name=ahci; gsi=16; trig=level; vec=80; marker=185
    else
        name='pata primary'; gsi=14; trig=edge; vec=82; marker=186
    fi
    if [ "$mode" = masked ]; then
        cp "$E2E_IMAGE" "$E2E_TMP/os.img"
        E2E_IMAGE="$E2E_TMP/os.img"
        printf 'samos.disk_irq_masked\n' > "$E2E_TMP/CMDLINE.TXT"
        MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
            || e2e_fail "mcopy CMDLINE.TXT"
    fi
    local boot_dump="$E2E_TMP/markers-boot.bin"
    _diskirq_mon() {
        shell_boot || { printf 'quit\n'; return 0; }
        e2e_pmemsave "$boot_dump"
        sleep 1
        shell_cmd 'echo diskirq written > 1:/DIRQ.TXT'
        shell_cmd 'cat 1:/DIRQ.TXT'
        printf 'info pic\n'
        printf 'info irq\n'
        e2e_pmemsave
        sleep 1
        printf 'quit\n'
    }
    e2e_run_qemu 300 _diskirq_mon
    [ -s "$boot_dump" ] && [ -s "$E2E_DUMP" ] || e2e_fail "$m: no marker dumps"

    local report="^disk: $name completions by interrupt, GSI $gsi $trig active-high, vector $vec"
    expect_serial '^e2e: user selftest done' "$m: disk reads and BLANK.ELF selftest"
    expect_serial "$SHELL_BANNER" "$m: shell up"
    expect_serial '^diskirq written' "$m: shell wrote and read back a file"
    expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "$m: no fault"

    local at_boot after polled
    after=$(marker_value "$marker")
    [ "$(marker_high 187)" = "$(expected_high)" ] && polled=$(marker_value 187) || polled=0
    local dump=$E2E_DUMP
    E2E_DUMP=$boot_dump
    at_boot=$(marker_value "$marker")
    [ "$(marker_high "$marker")" = "$(expected_high)" ] || at_boot=0
    E2E_DUMP=$dump
    [ "$(marker_high "$marker")" = "$(expected_high)" ] || after=0
    e2e_log "$m: completions at the interrupt: $at_boot after the boot, $after after the shell; polled: $polled"

    local entry
    entry=$(grep -E "^ *pin $gsi 0x" "$E2E_QEMU_LOG" | head -n 1)
    [ -n "$entry" ] || e2e_fail "$m: no IOAPIC pin $gsi in info pic"
    echo "$entry" | grep -E "vec=$vec +active-hi $trig " > /dev/null || e2e_fail "$m: IOAPIC pin $gsi is not vector $vec $trig: $entry"
    local raised
    raised=$(sed -n '/IRQ statistics for ioapic/,/IRQ statistics for/p' "$E2E_QEMU_LOG" | tr -d '\r' \
             | sed -nE "s/^ *$gsi: ([0-9]+).*/\1/p" | head -n 1)
    [ "${raised:-0}" -gt 0 ] || e2e_fail "$m: QEMU's IOAPIC never saw GSI $gsi assert"
    e2e_log "$m: info pic: $entry; info irq: GSI $gsi raised $raised times"

    if [ "$mode" = irq ]; then
        expect_serial "$report\$" "$m: $name routed through the IOAPIC"
        expect_no_serial 'completion interrupt missing' "$m: no fallback to polling"
        echo "$entry" | grep masked > /dev/null && e2e_fail "$m: IOAPIC pin $gsi masked: $entry"
        [ "$at_boot" -ge 10 ] || e2e_fail "$m: only $at_boot completions by interrupt during the boot"
        [ "$after" -gt "$at_boot" ] || e2e_fail "$m: the shell's file I/O took no interrupt ($at_boot -> $after)"
        [ "$polled" -eq 0 ] || e2e_fail "$m: $polled completions found by polling"
    else
        expect_serial "$report, line masked \(samos.disk_irq_masked\)\$" "$m: line routed but masked"
        expect_serial "^disk: $name completion interrupt missing, polling from now on\$" "$m: driver fell back to polling"
        echo "$entry" | grep masked > /dev/null || e2e_fail "$m: IOAPIC pin $gsi not masked: $entry"
        [ "$after" -eq 0 ] || e2e_fail "$m: $after completions by interrupt with the line masked"
        [ "$polled" -eq 3 ] || e2e_fail "$m: $polled waits gave up, expected 3 before polling for good"
    fi
}
