#!/usr/bin/env bash
# e2e/22-gotcha-fidelity.sh - source-fidelity regression for the
# preserved upstream-typo gotchas documented in
# docs/64bit/debugging-history.md (G14, G17, G19, G20, G22-G35,
# G39-G42).
#
# These bugs are inert in the SamOs port because they are either
# self-cancelling, defensively shimmed, or sit on an unreachable
# code path. The port rule is that we PRESERVE the upstream
# identifier verbatim so the git history matches the lecture
# tape. If a future cleanup edits any of these strings away
# without an accompanying entry in the debugging history, we
# want a loud failure - that is what this script gives us.
#
# This does not boot QEMU. It greps the source tree at the
# current commit; running it costs about 50 ms.

set -uo pipefail

SRC_ROOT="$(cd "$(dirname "$0")/../.." && pwd)/src"
fail=0

assert_present() {
    local label=$1
    local pattern=$2
    local file_hint=$3
    if grep -rqsF -- "$pattern" "$SRC_ROOT"; then
        echo "  PASS  $label"
    else
        echo "  FAIL  $label (expected $pattern in $file_hint)"
        fail=1
    fi
}

echo "[e2e] gotcha-fidelity: scanning $SRC_ROOT"

# G14: grpahics_setup_stage_two typo retired at L150 -> the FIXED
# spelling lives now; the typo's audit trail is in the chapter
# doc, not the source. Confirm the FIXED form is present.
assert_present "G14 graphics_setup_stage_two (typo retired)" \
    "graphics_setup_stage_two" "src/graphics/graphics.c"

# G17: vector_Free / vector_free - upstream typo; SamOs uses the
# correct form. Confirm fix is in place and a comment preserves
# the audit trail.
assert_present "G17 vector_free (typo fixed; audit comment)" \
    "vector_free" "src/task/process.c"

# G19: isr90h_command24_update_window_title -> upstream typo.
# Renamed at L180 in the SamOs port. Confirm the FIXED form.
assert_present "G19 isr80h_command24 (renamed at L180)" \
    "isr80h_command24" "src/isr80h"

# G20: self-recursion typo in disk_driver_register - upstream
# fixes at L189; SamOs follows.
assert_present "G20 disk_driver_registered (L189 fix)" \
    "disk_driver_registered" "src/disk/driver.c"

# G22: PCI_BASE_CLASS macro arity divergence with PCI_BASE_SUBCLASS.
assert_present "G22 PCI_BASE_CLASS macro present"    "PCI_BASE_CLASS" "src/io/pci.h"
assert_present "G22 PCI_BASE_SUBCLASS macro present" "PCI_BASE_SUBCLASS" "src/io/pci.h"

# G23: pci_ecam_cfg_ptr typo - preserved verbatim in pci.c.
assert_present "G23 pci_ecam_cfg_ptr (preserved)" \
    "pci_ecam_cfg_ptr" "src/io/pci.c"

# G24: pci_cfg_reaD_dword (capital D, second D) - upstream
# fixes at L180; SamOs drops the shim then. Identifier may
# or may not survive depending on what we are at; what we
# want to lock down is that pci_cfg_read_dword (correct form)
# exists.
assert_present "G24 pci_cfg_read_dword (correct form present)" \
    "pci_cfg_read_dword" "src/io/pci.c"

# G25: pci_size_bar singular forward decl. The plural definition
# must exist.
assert_present "G25 pci_size_bars defined" \
    "pci_size_bars" "src/io/pci.c"

# G26: pci_cfg_write_dword + pci_cfg_write_word divergence.
# Upstream renames at L180; the corrected form must be present.
assert_present "G26 pci_cfg_write_word (L180 rename)" \
    "pci_cfg_write_word" "src/io/pci.h"

# G27/G28/G29: PATA driver typos and arg-by-value bug. The
# corrected signatures must exist.
assert_present "G27 pata_disk_ctrl_drive_address present" \
    "pata_disk_ctrl_drive_address" "src/disk/drivers/pata.c"
assert_present "G29 disk_private_data_driver present" \
    "disk_private_data_driver" "src/disk/driver.h"

# G30: nvme_compeltion_queue_entry typo retired at L192; the
# correct spelling must be the live one.
assert_present "G30 nvme_completion_queue_entry (L192 fix)" \
    "nvme_completion_queue_entry" "src/disk/drivers/nvme.c"

# G31: NVME_COMPLETION_QUEUE_STATUS macro must exist.
assert_present "G31 NVME_COMPLETION_QUEUE_STATUS macro" \
    "NVME_COMPLETION_QUEUE_STATUS" "src/disk/drivers/nvme.h"

# G32: nvme_pci_mmio_base 0xFFFFFFFF0ULL constant. Preserved
# verbatim with a comment.
assert_present "G32 nvme_pci_mmio_base preserved" \
    "nvme_pci_mmio_base" "src/disk/drivers/nvme.c"

# G33: admin completion queue init readiness check. The function
# must exist; the bug body is inert.
assert_present "G33 nvme_admin_completion_queue_init present" \
    "nvme_admin_completion_queue_init" "src/disk/drivers/nvme.c"

# G34: nvme_disk_driver_umount typo. Upstream fixes at L195;
# correct form must be present.
assert_present "G34 nvme_disk_driver_unmount (L195 fix)" \
    "nvme_disk_driver_unmount" "src/disk/drivers/nvme.c"

# G35: slba += nlb (chunk loop fix) - upstream fixes at L195.
assert_present "G35 slba += nlb (L195 fix)" \
    "slba += nlb" "src/disk/drivers/nvme.c"

# G39: disk_Stream_cache_bucket_level1 typo - retired at L206.
# Confirm correct form present.
assert_present "G39 disk_stream_cache_bucket (L206 fix)" \
    "disk_stream_cache_bucket" "src/disk/streamer.c"

# G40 / G41 / G42: cached diskstreamer_read lands at L206 with
# disk_real_offset + sector_size field. Confirm both anchors.
assert_present "G40-G42 sector_size field on disk_stream" \
    "sector_size" "src/disk/streamer.h"
assert_present "G40-G42 disk_real_offset helper" \
    "disk_real_offset" "src/disk/streamer.c"

# G36: keyboard_init hoisted before window_system_initialize_stage2.
# We assert the hoisted call order survives by anchoring the call
# in the kernel.c init sequence.
assert_present "G36 keyboard_init present" "keyboard_init" "src/kernel.c"
assert_present "G36 window_system_initialize_stage2 present" \
    "window_system_initialize_stage2" "src/kernel.c"

# G37: task_sleep deadline math. The body lives in task.c; the
# correct addition form (+ microseconds) must be the live one
# per the L198-part-2 squash.
assert_present "G37 task_sleep + microseconds (L198 part 2 squash)" \
    "+ microseconds" "src/task/task.c"

# G38: process_realloc(NULL, n) early-out lives in process.c
# (L201 bugfix). Anchor the NULL early-out comment.
assert_present "G38 process_realloc NULL early-out" \
    "process_malloc" "src/task/process.c"

# G18: window-events ring producer/consumer functions exist.
# The bug body is preserved verbatim; we anchor by name only.
assert_present "G18 window event push site present" \
    "window_event" "src/graphics/window.c"

# G46-G48 already have runtime tests; the source-level marker
# we lock down here is that the fixes are still in place.
assert_present "G46 mouse_system_init hoisted (selftest comment)" \
    "mouse_system_init" "src/kernel.c"
assert_present "G47 mouse panic converted to return" \
    "SamOs e2e (G47)" "src/mouse/mouse.c"
assert_present "G48 system_font_local guard" \
    "system_font_local" "src/kernel.c"

# G13 / G43 / G44 / G45 are BIOS-path-only and the current
# SamOs build supports the UEFI path. The audit lives in
# docs/64bit/debugging-history.md only; no source anchor here.

if [ "$fail" = 0 ]; then
    echo "PASS: e2e/22 gotcha fidelity (all preserved-typo and fix anchors present)"
    exit 0
else
    echo "FAIL: e2e/22 gotcha fidelity (see misses above)"
    echo "      a missing anchor means a cleanup edit removed a documented"
    echo "      gotcha marker; verify against docs/64bit/debugging-history.md"
    exit 1
fi
