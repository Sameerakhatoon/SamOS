#ifndef FAT16_H
#define FAT16_H

#include "fs/file.h"

struct filesystem* fat16_init();
int                fat16_check_sizes();
// Returns the total number of valid items in the root directory of the
// disk's FAT16 mount, or -1 if no FAT16 filesystem is mounted.
int                fat16_root_dir_total(struct disk* disk);

#endif
