#ifndef KERNEL_DISK_MBR_H
#define KERNEL_DISK_MBR_H
// Classic MBR partition table (the 4 primary entries).
// Used for disks that have neither a GPT nor a filesystem on
// the whole disk, e.g. USB sticks and SD cards formatted by
// Windows or Linux.

#include <stdint.h>

struct mbr_partition_entry {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_first;
    uint32_t sector_count;
} __attribute__((packed));

struct disk;

// Returns the number of partitions created, 0 if the disk has
// no MBR partition table, or a negative status.
int mbr_scan_disk(struct disk* disk);

#endif
