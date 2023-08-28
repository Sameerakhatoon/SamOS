// MBR partition table scan. Extended partitions are not
// followed; only the 4 primary entries are used.

#include "mbr.h"
#include "disk/disk.h"
#include "memory/memory.h"
#include "status.h"

#define MBR_TYPE_EMPTY       0x00
#define MBR_TYPE_EXTENDED    0x05
#define MBR_TYPE_EXTENDED_LBA 0x0F
#define MBR_TYPE_GPT_PROTECT 0xEE

int mbr_scan_disk(struct disk* disk){
    int ss = disk->sector_size;
    uint8_t sector[ss];
    int created = 0;

    if(ss < 512 || disk_read_block(disk, 0, 1, sector) < 0){
        return -EIO;
    }
    if(sector[510] != 0x55 || sector[511] != 0xAA){
        return 0;
    }
    for(int i = 0; i < 4; i++){
        struct mbr_partition_entry entry;
        memcpy(&entry, sector + 446 + i * sizeof(entry), sizeof(entry));
        // A FAT boot sector also ends in 55 AA; its bytes at 446
        // are boot code. Valid entries have status 0x00 or 0x80.
        if(entry.status != 0x00 && entry.status != 0x80){
            return created;
        }
        if(entry.type == MBR_TYPE_EMPTY || entry.type == MBR_TYPE_EXTENDED ||
           entry.type == MBR_TYPE_EXTENDED_LBA || entry.type == MBR_TYPE_GPT_PROTECT ||
           entry.lba_first == 0 || entry.sector_count == 0){
            continue;
        }
        uint64_t first = entry.lba_first;
        uint64_t last  = first + entry.sector_count - 1;
        if(disk_create_partition(disk, first, last, NULL) == 0){
            created++;
        }
    }
    return created;
}
