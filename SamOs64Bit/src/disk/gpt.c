// Lecture 77 - read and decode the GPT partition table.
//
// GPT layout on disk:
//   LBA 0 - protective MBR (legacy compat)
//   LBA 1 - primary GPT header
//   LBA 2..N - partition entry array
//   ...
//   LBA -33..-2 - backup partition entry array
//   LBA -1 - backup GPT header
//
// Changes:
//   - Runs on every REAL disk (the course only scanned disk 0).
//   - Checks the header CRC32 and the entry-array CRC32, and
//     falls back to the backup header if the primary is bad.
//   - 64-bit LBAs end to end (the course truncated the entry
//     array's byte offset to int).
//   - Partition disks reuse the parent disk's driver, so reads
//     no longer fall back to hard-coded IDE ports.

#include "gpt.h"
#include "disk/disk.h"
#include "status.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "kernel.h"

// Refuse silly entry arrays (the spec minimum is 16 KiB; nobody
// uses more than a few hundred KiB).
#define GPT_MAX_ARRAY_BYTES (1024 * 1024)

static int gpt_partitions_found = 0;

uint32_t gpt_crc32(const void* data, size_t len){
    const uint8_t* p = data;
    uint32_t crc = 0xFFFFFFFFu;
    for(size_t i = 0; i < len; i++){
        crc ^= p[i];
        for(int k = 0; k < 8; k++){
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
        }
    }
    return ~crc;
}

// Read and validate the header at `lba`. On success *array_out
// holds the (CRC-checked) entry array; the caller frees it.
static int gpt_read_valid(struct disk* disk, uint64_t lba,
                          struct gpt_partition_table_header* header_out,
                          void** array_out){
    int res = 0;
    int ss = disk->sector_size;
    char sector[ss];
    void* array = NULL;

    res = disk_read_block(disk, lba, 1, sector);
    if(res < 0){
        return res;
    }
    struct gpt_partition_table_header* header = (struct gpt_partition_table_header*)sector;
    if(memcmp(header->signature, GPT_SIGNATURE, sizeof(header->signature)) != 0){
        return -EINFORMAT;
    }
    if(header->hdr_size < 92 || header->hdr_size > (uint32_t)ss || header->lba != lba){
        return -EINFORMAT;
    }
    uint32_t stored_crc = header->cr32_checksum;
    header->cr32_checksum = 0;
    uint32_t crc = gpt_crc32(header, header->hdr_size);
    header->cr32_checksum = stored_crc;
    if(crc != stored_crc){
        return -EINFORMAT;
    }

    uint64_t entry_size = header->array_entry_size;
    uint64_t array_bytes = entry_size * header->total_array_entries;
    if(entry_size < sizeof(struct gpt_partition_entry) || (entry_size % 8) != 0 ||
       array_bytes == 0 || array_bytes > GPT_MAX_ARRAY_BYTES){
        return -EINFORMAT;
    }
    int array_sectors = (int)((array_bytes + ss - 1) / ss);
    array = kzalloc((size_t)array_sectors * ss);
    if(!array){
        return -ENOMEM;
    }
    res = disk_read_block(disk, header->guid_array_lba_start, array_sectors, array);
    if(res < 0){
        kfree(array);
        return res;
    }
    if(gpt_crc32(array, array_bytes) != header->crc32_entry_array){
        kfree(array);
        return -EINFORMAT;
    }

    memcpy(header_out, header, sizeof(*header_out));
    *array_out = array;
    return 0;
}

int gpt_scan_disk(struct disk* disk){
    struct gpt_partition_table_header header;
    void* array = NULL;
    int created = 0;

    if(!disk || disk->type != SAMOS_DISK_TYPE_REAL){
        return -EINVARG;
    }

    int res = gpt_read_valid(disk, GPT_PARTITION_TABLE_HEADER_LBA, &header, &array);
    if(res < 0){
        // Primary damaged: the backup header sits in the last
        // sector. Only possible when the driver knows the size.
        if(disk->total_sectors < 3){
            return 0;
        }
        res = gpt_read_valid(disk, disk->total_sectors - 1, &header, &array);
        if(res < 0){
            return 0;
        }
        print("gpt: primary header bad, using backup\n");
    }

    for(uint32_t i = 0; i < header.total_array_entries; i++){
        struct gpt_partition_entry* entry =
            (struct gpt_partition_entry*)((char*)array + (size_t)i * header.array_entry_size);
        char guid_empty[16] = {0};
        if(memcmp(entry->guid, guid_empty, sizeof(entry->guid)) == 0){
            continue;
        }
        if(entry->ending_lba < entry->starting_lba ||
           entry->starting_lba < header.data_block_lba ||
           entry->ending_lba > header.data_block_lba_end){
            continue;
        }
        if(disk_create_partition(disk, entry->starting_lba, entry->ending_lba, NULL) == 0){
            created++;
        }
    }
    kfree(array);
    gpt_partitions_found += created;
    return created;
}

int gpt_init(void){
    return gpt_partitions_found > 0 ? 0 : -EINFORMAT;
}
