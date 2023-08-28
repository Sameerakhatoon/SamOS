#ifndef KERNEL_DISK_STORAGE_POLICY_H
#define KERNEL_DISK_STORAGE_POLICY_H
// Storage write policy.
//
// SamOS can now write disks. Under QEMU (or any hypervisor) all
// disks are writable, because they are scratch image files. On
// real hardware every disk is an internal disk (there is no USB
// mass storage yet), and SamOS must never
// write to the laptop's internal NVMe. So on bare metal every
// disk is mounted read-only unless the kernel is built with
//
//     make EXTRA_CFLAGS=-DSAMOS_STORAGE_WRITE_REAL_HW=1
//
// Only do that on a machine whose disks hold nothing you need
// (a spare test PC).
//
// SAMOS_STORAGE_FORCE_READ_ONLY=1 makes every disk read-only,
// even under a hypervisor.

#ifndef SAMOS_STORAGE_WRITE_REAL_HW
#define SAMOS_STORAGE_WRITE_REAL_HW 0
#endif

#ifndef SAMOS_STORAGE_FORCE_READ_ONLY
#define SAMOS_STORAGE_FORCE_READ_ONLY 0
#endif

#endif
