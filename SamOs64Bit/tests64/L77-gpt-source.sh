#!/bin/bash
# tests64/L77-gpt-source.sh
#
# Lecture 77 - GPT header + partition entry layout, reader,
# and per-partition virtual-disk mounter.
#
# gpt.c / gpt.h NOT in the build (depends on disk_create_new +
# SAMOS_DISK_TYPE_PARTITION which L78 adds). This test just
# confirms the source is present and structurally correct.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

[ -f src/disk/gpt.h ] || { echo "FAIL: gpt.h missing"; exit 1; }
[ -f src/disk/gpt.c ] || { echo "FAIL: gpt.c missing"; exit 1; }

grep -q 'struct gpt_partition_table_header' src/disk/gpt.h \
    || { echo "FAIL: gpt header struct missing"; exit 1; }
grep -q 'struct gpt_partition_entry' src/disk/gpt.h \
    || { echo "FAIL: gpt entry struct missing"; exit 1; }
grep -q 'EFI PART' src/disk/gpt.h \
    || { echo "FAIL: EFI PART signature missing"; exit 1; }

grep -q 'gpt_init' src/disk/gpt.c \
    || { echo "FAIL: gpt_init missing"; exit 1; }
grep -q 'gpt_mount_partitions' src/disk/gpt.c \
    || { echo "FAIL: gpt_mount_partitions missing"; exit 1; }

[ -f bin/kernel.bin ] || { echo "FAIL: bin/kernel.bin missing"; exit 1; }
exit 0
