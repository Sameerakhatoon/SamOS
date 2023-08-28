#ifndef DISKSTREAMER_H
#define DISKSTREAMER_H
// Lecture 83 / 203 - disk streamer: byte-granular reads (and,
// since the storage work, writes) over a disk view, through a per-hardware-
// disk sector cache.
//
// Replaced the course's four-level cache tree. That tree
// freed a level-3 bucket on round-robin eviction while level 2
// still pointed at it (use after free), copied the wrong number
// of bytes when a short read crossed a sector boundary, and used
// int byte positions (2 GiB limit). The new cache is a direct-
// mapped, write-through table keyed by absolute LBA on the
// hardware disk, so every partition view of the same sectors
// sees the same data.

#include <stdint.h>
#include <stdbool.h>
#include "disk.h"

#define DISK_STREAMER_MAX_CACHE_SECTOR_SIZE 4096
// Direct-mapped slots per hardware disk. 1024 * 512 B = 512 KiB.
#define DISK_STREAM_CACHE_SLOTS             1024

struct disk_stream_cache_slot {
    uint64_t lba;      // absolute LBA on the hardware disk
    bool     valid;
    char*    buf;      // sector_size bytes, allocated on first use
};

struct disk_stream_cache {
    struct disk_stream_cache_slot slots[DISK_STREAM_CACHE_SLOTS];
    uint64_t hits;
    uint64_t misses;
};

struct disk_stream {
    int64_t       pos;
    int           sector_size;
    struct disk*  disk;
};

struct disk_stream_cache* diskstreamer_cache_new();
// Called by disk_write_block: refresh (ok=true) or drop
// (ok=false) any cached copies of the written sectors.
void diskstreamer_cache_update(struct disk* hw, uint64_t abs_lba, int total,
                               const void* data, bool ok);

struct disk_stream* diskstreamer_new(int disk_id);
// Lecture 83 - bind a streamer directly to a disk pointer
// (e.g. a partition view).
struct disk_stream* diskstreamer_new_from_disk(struct disk* disk);
// Returns -EINVARG for a negative position.
int                 diskstreamer_seek(struct disk_stream* stream, int64_t pos);
// Both return 0 on success (all bytes moved) or a negative
// status. The position advances by the bytes moved.
int                 diskstreamer_read(struct disk_stream* stream, void* out, int total);
int                 diskstreamer_write(struct disk_stream* stream, const void* in, int total);
void                diskstreamer_close(struct disk_stream* stream);

#endif
