#ifndef KERNEL_FS_STORAGE_SELFTEST_H
#define KERNEL_FS_STORAGE_SELFTEST_H
// Storage self-test, driven by tests64/storage/run.sh.
//
// At boot it prints one line per disk, then looks for disks whose
// FAT volume label is STORAGE_SELFTEST_LABEL. Those are scratch
// images made by the host script; no other disk is ever written.
// On each one it reads /TESTCMD.TXT:
//   "WRITE"  - create directories and files (long names, sparse,
//              append, truncate, delete, a directory that grows
//              past one cluster), read them back, check files
//              that Linux put in /FROMLNX, write /RESULT1.TXT;
//   "VERIFY" - after a reboot: check everything WRITE made is
//              still there, write /RESULT2.TXT.
// The host then checks the files with mtools and fsck.fat.

#define STORAGE_SELFTEST_LABEL "SMTEST     "

int storage_selftest(void);

#endif
