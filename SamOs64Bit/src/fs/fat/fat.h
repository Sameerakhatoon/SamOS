#ifndef FAT_H
#define FAT_H
// FAT12/16/32 filesystem driver with long file names and
// write support. Replaces the course's read-only FAT16 driver
// (fat16.c). The type is chosen from the BPB by cluster count,
// as the Microsoft FAT specification requires.

#include <stdint.h>
#include "fs/file.h"

struct disk;

struct filesystem* fat_init(void);

// FAT type of a mounted disk (12, 16 or 32), or -1 when the disk
// has no FAT filesystem.
int  fat_type(struct disk* disk);

// Timestamps written into new and modified entries. Each one asks
// the time source (seconds since 1970, or -1; the kernel passes
// rtc_unix_time at boot) and falls back to the fixed DOS date and
// time set here, a built-in date by default.
void fat_set_clock(uint16_t dos_date, uint16_t dos_time);
void fat_set_time_source(int64_t (*now)(void));

// Checks the packed on-disk struct sizes; 1 when all match.
int  fat_check_sizes(void);

#endif
