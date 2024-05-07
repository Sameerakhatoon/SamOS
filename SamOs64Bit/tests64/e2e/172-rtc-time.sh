#!/usr/bin/env bash
# e2e-requires: x86 (the CMOS RTC)
# e2e/172-rtc-time.sh - the CMOS RTC gives the wall-clock
# time. QEMU starts its RTC from the host clock in UTC, so the unix
# time the kernel prints at boot lies between the host time before
# QEMU started and after it ended, and the printed date matches it.
. "$(dirname "$0")/_lib.sh"

before=$(date -u +%s)
boot_and_dump 30 '^rtc: '
after=$(date -u +%s)
expect_serial '^rtc: [0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} UTC, unix [0-9]+$' "RTC line"
line=$(tr -d '\r' < "$E2E_SERIAL" | grep -E '^rtc: ' | head -n 1)
unix=${line##* }
stamp=$(echo "$line" | sed -E 's/^rtc: (.*) UTC, unix .*/\1/')
e2e_log "host $before..$after, kernel $unix ($stamp)"
[ "$unix" -ge $((before - 5)) ] && [ "$unix" -le $((after + 5)) ] \
    || e2e_fail "RTC unix time $unix outside host window $before..$after"
[ "$(date -u -d "@$unix" '+%Y-%m-%d %H:%M:%S')" = "$stamp" ] \
    || e2e_fail "printed date $stamp does not match unix $unix"
echo "PASS: e2e/172 rtc time"
