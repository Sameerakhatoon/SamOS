#!/bin/bash
# tests64/L78-virtual-disks.sh
#
# Lecture 78 - virtual disks + vector library.
#
# NEW src/lib/vector/{vector.h, vector.c} - generic dynamic
# array. Used by L78's disk restructure (vector of struct disk*)
# and later by process / IRQ / etc lists.
#
# disk.h additions:
#   SAMOS_DISK_TYPE_PARTITION (= 1)
#   SAMOS_KERNEL_FILESYSTEM_NAME ("SAMOS      ")
#   starting_lba / ending_lba fields on struct disk
#   disk_create_new + disk_primary + disk_primary_fs_disk
#     prototypes
#
# disk.c restructure:
#   disk_array[4] gone. disk_vector = vector of disk*.
#   disk_create_new: kzalloc + populate + try to attach fs +
#     vector_push.
#   disk_search_and_init: vector_new + disk_create_new(REAL).
#   disk_read_block: bounds-checked LBA via starting_lba /
#     ending_lba.
#
# SamOs deviations:
#   - krealloc temp stub added in kheap.c so vector.c can
#     link until L80 brings the real version.
#   - filesystem->volume_name call wrapped in #if 0 because
#     volume_name is added in L84. primary_fs_disk just takes
#     the first disk for now.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

# Source files present.
for f in src/lib/vector/vector.h src/lib/vector/vector.c \
         src/types.h; do
    [ -f "$f" ] || { echo "FAIL: $f missing"; exit 1; }
done

# vector library symbols compiled into the build.
[ -f build/lib/vector/vector.o ] \
    || { echo "FAIL: build/lib/vector/vector.o missing"; exit 1; }

# disk.h shape.
grep -q '^#define SAMOS_DISK_TYPE_PARTITION' src/disk/disk.h \
    || { echo "FAIL: SAMOS_DISK_TYPE_PARTITION missing"; exit 1; }
grep -q '^#define SAMOS_KERNEL_FILESYSTEM_NAME' src/disk/disk.h \
    || { echo "FAIL: SAMOS_KERNEL_FILESYSTEM_NAME missing"; exit 1; }
grep -q 'starting_lba' src/disk/disk.h \
    || { echo "FAIL: starting_lba field missing"; exit 1; }
grep -q 'int.*disk_create_new' src/disk/disk.h \
    || { echo "FAIL: disk_create_new prototype missing"; exit 1; }

# disk.c uses the vector library.
grep -q 'vector_new' src/disk/disk.c \
    || { echo "FAIL: vector_new call missing in disk.c"; exit 1; }
grep -q 'vector_push' src/disk/disk.c \
    || { echo "FAIL: vector_push call missing in disk.c"; exit 1; }

[ -f bin/kernel.bin ] || { echo "FAIL: bin/kernel.bin missing"; exit 1; }
exit 0
