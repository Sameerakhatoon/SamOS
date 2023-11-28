#ifndef DISK_H
#define DISK_H

#include <stddef.h>
#include "fs/file.h"
#include "driver.h"           // L184 - struct disk_driver

typedef unsigned int SAMOS_DISK_TYPE;

// Represents a real physical hard disk.
#define SAMOS_DISK_TYPE_REAL        0

// Lecture 78 - virtual disk over a partition.
#define SAMOS_DISK_TYPE_PARTITION   1

// Lecture 78 - the volume label our kernel filesystem uses.
// disk_create_new sets primary_fs_disk to the disk whose
// FAT16 volume label matches.
#define SAMOS_KERNEL_FILESYSTEM_NAME "SAMOS      "

// Lecture 183 - forward decl for the driver pointer added below.
struct disk_driver;

struct disk {
    SAMOS_DISK_TYPE     type;
    int                 sector_size;
    int                 id;
    struct filesystem*  filesystem;

    // Lecture 184 - owning disk driver (NULL for the bootstrap
    // disk0 that predates the abstraction).
    struct disk_driver* driver;

    // Lecture 183 - back-pointer to the physical disk this disk
    // is hosted on. Partition disks point at their hardware
    // disk; REAL disks point at themselves.
    struct disk*        hardware_disk;

    // Lecture 78 - LBA bounds. Set both to zero for the
    // primary disk; all bounds checking is then bypassed.
    size_t              starting_lba;
    size_t              ending_lba;

    void*               fs_private;

    // Lecture 183 - disk-driver-side opaque pointer (PATA per-
    // channel state, NVME submission queue, etc).
    void*               driver_private;
};

void         disk_search_and_init();
struct disk* disk_get(int index);
int          disk_read_block(struct disk* idisk, unsigned int lba, int total, void* buf);

// Lecture 78 / 183 - virtual-disk constructor. L183 widens the
// signature with a `disk_driver*`, a `hardware_disk*`, and a
// `driver_private` pointer. type=REAL forces hardware_disk=self.
int          disk_create_new(struct disk_driver* driver,
                             struct disk* hardware_disk,
                             int type, int starting_lba, int ending_lba,
                             size_t sector_size,
                             void* driver_private_data,
                             struct disk** disk_out);

// Lecture 183 - accessor for the disk's hardware backing.
struct disk* disk_hardware_disk(struct disk* disk);

// Lecture 186 - run fs_resolve against the disk and (if the
// label matches) set primary_fs_disk.
int          disk_filesystem_mount(struct disk* disk);

// Lecture 186 - delegate partition creation to the disk's
// driver.
int          disk_create_partition(struct disk* disk,
                                   int starting_lba, int ending_lba,
                                   struct disk** partition_disk_out);

struct disk* disk_primary(void);
struct disk* disk_primary_fs_disk(void);

#endif
