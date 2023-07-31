#!/usr/bin/env bash
# L86 - UEFI app queries GOP, paints screen green, hands FB info
# to the kernel through rdi/rsi/rdx/rcx.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PROJ=$(cd "$ROOT/.." && pwd)

fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q '#include <Protocol/GraphicsOutput.h>' "$PROJ/SamOs.c" \
    || fail "SamOs.c: missing GraphicsOutput.h include"
grep -q 'GetFrameBufferInfo' "$PROJ/SamOs.c" \
    || fail "SamOs.c: GetFrameBufferInfo helper missing"
grep -q 'gEfiGraphicsOutputProtocolGuid' "$PROJ/SamOs.c" \
    || fail "SamOs.c: GOP guid not referenced"
grep -q 'gEfiGraphicsOutputProtocolGuid' "$PROJ/SamOs.inf" \
    || fail "SamOs.inf: GOP guid not listed under [Protocols]"

# FB params must be passed via the SysV regs before jmp.
grep -q 'movq %0, %%rdi' "$PROJ/SamOs.c" \
    || fail "SamOs.c: rdi load missing"
grep -q 'movq %1, %%rsi' "$PROJ/SamOs.c" \
    || fail "SamOs.c: rsi load missing"
grep -q 'movq %2, %%rdx' "$PROJ/SamOs.c" \
    || fail "SamOs.c: rdx load missing"
grep -q 'movq %3, %%rcx' "$PROJ/SamOs.c" \
    || fail "SamOs.c: rcx load missing"

# Green sanity-paint loop survives.
grep -q '.Green    = 0xff\|.Green = 0xff' "$PROJ/SamOs.c" \
    || fail "SamOs.c: green sanity paint missing"

# Order: ExitBootServices BEFORE the register loads + jmp.
awk '/ExitBootServices\(/{e=NR} /movq %0, %%rdi/{if(!e || NR<e) exit 1} /jmp \*/{if(!e || NR<e) exit 1; exit 0}' \
    "$PROJ/SamOs.c" || fail "SamOs.c: ExitBootServices must precede the asm jump"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"

echo "PASS: L86 UEFI framebuffer"
