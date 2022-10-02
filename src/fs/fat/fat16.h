#ifndef FAT16_H
#define FAT16_H

#include "fs/file.h"

struct filesystem* fat16_init();
int                fat16_check_sizes();

#endif
