#ifndef DISK_H
#define DISK_H

#include <stddef.h>
#include "fs/file.h"

typedef unsigned int SAMOS_DISK_TYPE;

// Represents a real physical hard disk.
#define SAMOS_DISK_TYPE_REAL        0

// Lecture 78 - virtual disk over a partition.
#define SAMOS_DISK_TYPE_PARTITION   1

// Lecture 78 - the volume label our kernel filesystem uses.
// disk_create_new sets primary_fs_disk to the disk whose
// FAT16 volume label matches.
#define SAMOS_KERNEL_FILESYSTEM_NAME "SAMOS      "

struct disk {
    SAMOS_DISK_TYPE     type;
    int                 sector_size;
    int                 id;
    struct filesystem*  filesystem;

    // Lecture 78 - LBA bounds. Set both to zero for the
    // primary disk; all bounds checking is then bypassed.
    size_t              starting_lba;
    size_t              ending_lba;

    void*               fs_private;
};

void         disk_search_and_init();
struct disk* disk_get(int index);
int          disk_read_block(struct disk* idisk, unsigned int lba, int total, void* buf);

// Lecture 78 - virtual-disk constructor. Pushes a new struct disk
// onto the disk_vector. type=REAL for physical, PARTITION for a
// virtual disk that spans a GPT entry's LBA range.
int          disk_create_new(int type, int starting_lba, int ending_lba,
                             size_t sector_size, struct disk** disk_out);

struct disk* disk_primary(void);
struct disk* disk_primary_fs_disk(void);

#endif
