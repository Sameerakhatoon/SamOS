#ifndef DISK_H
#define DISK_H

#include "lib/spinlock/spinlock.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "fs/file.h"
#include "driver.h"           // L184 - struct disk_driver

typedef unsigned int SAMOS_DISK_TYPE;

// Represents a real physical hard disk.
#define SAMOS_DISK_TYPE_REAL        0

// Lecture 78 - virtual disk over a partition.
#define SAMOS_DISK_TYPE_PARTITION   1

// Lecture 78 - the volume label our kernel filesystem uses.
// disk_filesystem_mount sets primary_fs_disk to the disk whose
// FAT volume label matches.
#define SAMOS_KERNEL_FILESYSTEM_NAME "SAMOS      "

// Lecture 183 - forward decl for the driver pointer added below.
struct disk_driver;
// Lecture 203 - forward decl for the per-disk stream cache.
struct disk_stream_cache;

struct disk {
    SAMOS_DISK_TYPE     type;
    int                 sector_size;
    int                 id;
    struct filesystem*  filesystem;

    // Lecture 184 - owning disk driver. Partitions inherit the
    // driver of the REAL disk they live on.
    struct disk_driver* driver;

    // Lecture 183 - back-pointer to the physical disk this disk
    // is hosted on. Partition disks point at their hardware
    // disk; REAL disks point at themselves.
    struct disk*        hardware_disk;

    // Sector cache. Only the REAL disk owns one; partition
    // disks share it through hardware_disk so a write through
    // one view is seen by every other view of the same sectors.
    struct disk_stream_cache* cache;

    // LBA bounds, 64-bit. starting_lba is absolute on the
    // hardware disk. total_sectors is the size of this view
    // (0 = unknown, no bounds check; only for REAL disks whose
    // driver could not report a size).
    uint64_t            starting_lba;
    uint64_t            ending_lba;     // inclusive, kept for GPT symmetry
    uint64_t            total_sectors;

    // Writes are refused when set. See disk_policy_* in
    // disk.c: internal disks on real hardware start read-only.
    bool                read_only;

    // The device went away (a USB stick pulled out): set on the
    // REAL disk and every partition view of it by disk_mark_gone. Its
    // files no longer open and its blocks fail at once, without a
    // driver call. The struct and its id stay (nothing frees disks).
    volatile bool       gone;

    void*               fs_private;

    // Lecture 183 - disk-driver-side opaque pointer (PATA per-
    // drive state, NVME queues, AHCI port state). Partitions
    // copy their parent's pointer.
    void*               driver_private;
};

// One CPU at a time in any disk driver and sector cache
// (disk.c); held around every driver call and stream read or write.
extern struct spinlock disk_lock;

int          disk_search_and_init();
int          disk_mount_all();
struct disk* disk_get(int index);
int          disk_count(void);

// Sector I/O relative to the disk view. Partitions are
// translated to absolute LBAs and routed through the hardware
// disk's driver. Bounds are checked against total_sectors.
int          disk_read_block(struct disk* idisk, uint64_t lba, int total, void* buf);
int          disk_write_block(struct disk* idisk, uint64_t lba, int total, const void* buf);

// Lecture 78 / 183 - virtual-disk constructor. type=REAL forces
// hardware_disk=self. total_sectors may be 0 for "unknown".
int          disk_create_new(struct disk_driver* driver,
                             struct disk* hardware_disk,
                             int type, uint64_t starting_lba, uint64_t total_sectors,
                             size_t sector_size,
                             void* driver_private_data,
                             struct disk** disk_out);

struct disk* disk_hardware_disk(struct disk* disk);

// Lecture 186 - run fs_resolve against the disk and (if the
// label matches) set primary_fs_disk.
int          disk_filesystem_mount(struct disk* disk);

// make a partition view of a REAL disk. LBAs are
// relative to the REAL disk; ending_lba is inclusive.
int          disk_create_partition(struct disk* disk,
                                   uint64_t starting_lba, uint64_t ending_lba,
                                   struct disk** partition_disk_out);

void*        disk_private_data_driver(struct disk* disk);
// A removable REAL disk went away: mark it and its partition
// views gone. Returns how many partition views there were.
int          disk_mark_gone(struct disk* hardware_disk);

// Lecture 206 - absolute-LBA + byte-offset accessors.
uint64_t     disk_real_sector(struct disk* idisk, uint64_t lba);
uint64_t     disk_real_offset(struct disk* idisk, uint64_t lba);

struct disk* disk_primary(void);
struct disk* disk_primary_fs_disk(void);

// True when the kernel runs under a hypervisor (CPUID
// leaf 1 ECX bit 31). Used by the read-only policy.
bool         disk_running_on_hypervisor(void);

#endif
