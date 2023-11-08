#!/usr/bin/env bash
# e2e/180-shell-files-pc.sh - done criterion on pc (IDE): from
# the shell, mkdir, write, ls, cat on a FAT16 and a FAT32 volume,
# reboot on the same disks and read the files back, then Linux
# (mtools, fsck.fat) reads what SamOS wrote; SamOS reads a file Linux
# wrote. Error cases: mkdir existing, rmdir non-empty, rm a
# directory, cat a missing file. See _files_lib.sh.
E2E_MACHINE=pc
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_files_lib.sh"

files_e2e_main "pc/IDE"
echo "PASS: e2e/180 shell files on IDE, FAT16 and FAT32, across a reboot"
