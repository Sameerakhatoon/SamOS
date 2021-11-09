#ifndef KERNEL_GPT_H
#define KERNEL_GPT_H

#include <stdint.h>
#include <stddef.h>

// Lecture 77 - GPT (GUID Partition Table) layout. Replaces the
// 512-byte MBR partition table for UEFI disks.

#define GPT_MASTER_BOOT_RECORD_LBA      0   // Legacy MBR for compat
#define GPT_PARTITION_TABLE_HEADER_LBA  1   // Primary GPT header sits at LBA 1
#define GPT_SIGNATURE                   "EFI PART"

// Header at LBA 1.
struct gpt_partition_table_header {
    char     signature[8];          // "EFI PART"
    uint32_t revision;
    uint32_t hdr_size;
    uint32_t cr32_checksum;
    uint32_t reserved1;

    uint64_t lba;                   // LBA of THIS header
    uint64_t lba_alternate;         // LBA of backup header

    uint64_t data_block_lba;        // First usable LBA for data
    uint64_t data_block_lba_end;    // Last usable LBA for data

    char     guid[16];              // Disk GUID

    uint64_t guid_array_lba_start;  // LBA of the partition entry array
    uint32_t total_array_entries;   // count of entries in that array
    uint32_t array_entry_size;      // bytes per entry

    uint32_t crc32_entry_array;     // CRC32 of the array

    // Padding to fill the rest of the LBA. flexible array.
    char     reserved2[];
};

// Each partition entry in the array at guid_array_lba_start.
struct gpt_partition_entry {
    char     guid[16];                  // partition type GUID
    char     unique_partition_guid[16]; // unique per partition
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    char     partition_name[72];        // UTF-16LE
};

int gpt_init(void);

#endif
