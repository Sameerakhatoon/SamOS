#!/usr/bin/env bash
# e2e/30-graphics.sh - graphics_screen_info() returns a root with
# non-zero width and height (proves UEFI framebuffer injection
# from L86 landed and graphics_setup ran).
# The framebuffer mapping is height x pitch x 4 bytes, rounded
# up to a page and no more than the loader's fb_size (the book used
# the width, which maps 4 MiB at 1024x768 where 3 MiB is the screen).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 37 -eq 1 "graphics_screen_info() non-NULL"
expect_stage_value 38 -eq 1 "screen width + height > 0"
line=$(tr -d '\r' < "$E2E_SERIAL" | grep -aE '^graphics: framebuffer [0-9]+x[0-9]+ pitch' | head -n 1)
[[ $line =~ ^graphics:\ framebuffer\ ([0-9]+)x([0-9]+)\ pitch\ ([0-9]+),\ mapped\ ([0-9]+)\ KiB\ of\ ([0-9]+)\ KiB$ ]] \
    || e2e_fail "no framebuffer size line (got '$line')"
w=${BASH_REMATCH[1]} h=${BASH_REMATCH[2]} pitch=${BASH_REMATCH[3]}
mapped=${BASH_REMATCH[4]} fb=${BASH_REMATCH[5]}
want=$(( (h * pitch * 4 + 4095) / 4096 * 4 ))
[ "$fb" -gt 0 ] && [ "$want" -gt "$fb" ] && want=$(( fb / 4 * 4 ))
[ "$mapped" -eq "$want" ] || e2e_fail "framebuffer mapped $mapped KiB, want $want (${w}x${h} pitch $pitch, fb $fb KiB)"
e2e_log "framebuffer ${w}x${h} pitch $pitch: mapped $mapped KiB of $fb KiB: ok"
echo "PASS: e2e/30 graphics"
