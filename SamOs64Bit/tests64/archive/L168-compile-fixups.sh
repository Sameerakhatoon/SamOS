#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Forward decls hoisted in graphics.h.
for sym in 'struct process;' 'struct graphics_info;' 'struct interrupt_frame;'; do
    grep -q "$sym" "$ROOT/src/isr80h/graphics.h" \
        || fail "graphics.h: $sym forward decl missing"
done

# struct framebuffer_pixel forward decl at the top of process.h.
head -n 35 "$ROOT/src/task/process.h" | grep -q 'struct framebuffer_pixel;' \
    || fail "process.h: framebuffer_pixel forward decl not hoisted"

# SamOs L165 already shipped graphics.o in the FILES list - assert it stays there.
grep -q 'build/isr80h/graphics.o' "$ROOT/Makefile" \
    || fail "Makefile: graphics.o missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L168 compile fixups"
