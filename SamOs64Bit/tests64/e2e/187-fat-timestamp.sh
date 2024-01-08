#!/usr/bin/env bash
# e2e/187-fat-timestamp.sh - files SamOS writes carry the RTC
# time, not the FAT driver's built-in date. The shell writes a file,
# appends to it and makes a directory on a FAT16 data disk; on the
# host, mdir's times for both are within a few minutes of the host's
# UTC time (QEMU's RTC runs in UTC, -rtc base=utc, and FAT stores the
# clock's time as local time).
E2E_MACHINE=pc
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"

export MTOOLS_SKIP_CHECK=1
img="$E2E_TMP/fat16.img"
truncate -s 64M "$img"
mkfs.fat -F 16 -n STAMPVOL "$img" >/dev/null || e2e_fail "mkfs FAT16"
E2E_EXTRA_QEMU_ARGS="-rtc base=utc -drive file=$img,format=raw,if=none,id=f16 -device ide-hd,drive=f16,bus=ide.0,unit=1"

_stamp_boot() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd "write 1:/STAMP.TXT stamped"
    shell_cmd "echo more >> 1:/STAMP.TXT"
    shell_cmd "mkdir 1:/STDIR"
    shell_cmd "echo stamp-done"
    sleep 1
    printf 'quit\n'
}

before=$(date -u +%s)
e2e_run_qemu 600 _stamp_boot
after=$(date -u +%s)
tr -d '\r' < "$E2E_SERIAL" | grep -q '^stamp-done$' || e2e_fail "shell did not finish"

# mdir line: "STAMP    TXT        13 YYYY-MM-DD  HH:MM" (minutes only).
check_stamp() {
    local name=$1 line stamp t
    line=$(TZ=UTC mdir -i "$img" :: 2>/dev/null | grep -E "^$name ")
    [ -n "$line" ] || e2e_fail "mdir does not list $name"
    stamp=$(echo "$line" | grep -Eo '[0-9]{4}-[0-9]{2}-[0-9]{2} +[0-9]{1,2}:[0-9]{2}' | tr -s ' ')
    t=$(TZ=UTC date -d "$stamp" +%s 2>/dev/null) || e2e_fail "$name: cannot parse '$line'"
    e2e_log "$name: $stamp UTC ($t), host $before..$after"
    # Minute resolution in mdir, 2 s in FAT: allow 3 minutes either side.
    [ "$t" -ge $((before - 180)) ] && [ "$t" -le $((after + 180)) ] \
        || e2e_fail "$name time $stamp is not the host time ($(date -u -d "@$before" '+%F %H:%M') UTC)"
}
check_stamp STAMP
check_stamp STDIR
[ "$(mtype -i "$img" ::STAMP.TXT 2>/dev/null)" = "stamped
more" ] || e2e_fail "Linux does not read STAMP.TXT back"
fsck.fat -n "$img" >/dev/null 2>&1 || e2e_fail "fsck.fat -n not clean"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT' "no fault"
echo "PASS: e2e/187 FAT timestamps follow the RTC"
