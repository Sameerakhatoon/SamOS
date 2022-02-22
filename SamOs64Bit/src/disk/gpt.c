// Lecture 77 - read and decode the GPT partition table.
//
// GPT layout on disk:
//   LBA 0 - protective MBR (legacy compat)
//   LBA 1 - primary GPT header
//   LBA 2..N - partition entry array
//   ...
//   LBA -1 - backup partition entry array
//   LBA -N - backup GPT header
//
// gpt_init reads the header at LBA 1, validates the "EFI PART"
// signature, and walks the partition entry array creating one
// virtual disk per non-empty entry.
//
// SamOs DEVIATION: the upstream code references disk_create_new
// with PEACHOS_DISK_TYPE_PARTITION which is added in L78. This
// file therefore does not compile against L77 alone - it's
// staged on disk pending L78 wiring it up. Our Makefile does
// not yet add gpt.o to FILES.

#include "gpt.h"
#include "disk/disk.h"
#include "status.h"
#include "memory/memory.h"
#include "disk/streamer.h"
#include "kernel.h"

struct disk* gpt_primary_disk = NULL;

// Some headers have trailing padding past the documented size.
// Real size = sizeof(*header) + (declared - documented offset).
size_t gpt_partition_table_header_real_size(struct gpt_partition_table_header* header){
    return sizeof(*header) + (header->hdr_size - offsetof(struct gpt_partition_table_header, reserved2));
}

int gpt_partition_table_header_read(struct gpt_partition_table_header* header_out){
    int res = 0;
    // C99 VLA gives us a stack-allocated sector buffer. The
    // GPT primary header always sits in LBA 1, one sector.
    char sector[gpt_primary_disk->sector_size];
    res = disk_read_block(gpt_primary_disk, GPT_PARTITION_TABLE_HEADER_LBA, 1, sector);
    if(res < 0){
        goto out;
    }
    memcpy(header_out, sector, sizeof(*header_out));
out:
    return res;
}

int gpt_mount_partitions(struct gpt_partition_table_header* partition_header){
    int res = 0;
    size_t   total_entries = partition_header->total_array_entries;
    uint64_t starting_lba  = partition_header->guid_array_lba_start;
    uint64_t starting_byte = starting_lba * gpt_primary_disk->sector_size;
    size_t   entry_size    = partition_header->array_entry_size;

    struct disk_stream* streamer = diskstreamer_new(gpt_primary_disk->id);
    if(!streamer){
        res = -EINVARG;
        goto out;
    }

    res = diskstreamer_seek(streamer, (int)starting_byte);
    if(res < 0){
        goto out;
    }

    for(size_t i = 0; i < total_entries; i++){
        char buffer[entry_size];
        res = diskstreamer_read(streamer, buffer, sizeof(buffer));
        if(res < 0){
            goto out;
        }

        struct gpt_partition_entry* entry = (struct gpt_partition_entry*)buffer;
        char guid_empty[16] = {0};
        if(memcmp(entry->guid, guid_empty, sizeof(entry->guid)) == 0){
            // Empty slot - skip.
            continue;
        }

        // L77 - depends on disk_create_new + PEACHOS_DISK_TYPE_PARTITION
        // (= SAMOS_DISK_TYPE_PARTITION in our prefix scheme) which the
        // L78 virtual-disk patch adds.
        // Lecture 183 - new disk_create_new signature: pass
        // driver=NULL, hardware_disk=primary, driver_private=NULL.
        res = disk_create_new(NULL, gpt_primary_disk,
                              SAMOS_DISK_TYPE_PARTITION,
                              entry->starting_lba, entry->ending_lba,
                              gpt_primary_disk->sector_size, NULL, NULL);
        if(res < 0){
            goto out;
        }
    }
out:
    return res;
}

int gpt_init(void){
    int res = 0;
    struct gpt_partition_table_header partition_header = {0};

    gpt_primary_disk = disk_get(0);
    if(!gpt_primary_disk){
        res = -EINVARG;
        goto out;
    }

    res = gpt_partition_table_header_read(&partition_header);
    if(res < 0){
        goto out;
    }

    if(memcmp(partition_header.signature, GPT_SIGNATURE,
              sizeof(partition_header.signature)) != 0){
        // Not a GPT-formatted disk.
        res = -EINFORMAT;
        goto out;
    }

    // GPT confirmed - mount each non-empty entry as a virtual disk.
    res = gpt_mount_partitions(&partition_header);
    if(res < 0){
        goto out;
    }
out:
    return res;
}
