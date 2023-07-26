#include "streamer.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"   // L64 - memcpy
#include "config.h"
#include <stdbool.h>

struct disk_stream* diskstreamer_new(int disk_id){
    struct disk* disk = disk_get(disk_id);
    if(!disk){
        return 0;
    }

    struct disk_stream* streamer = kzalloc(sizeof(struct disk_stream));
    streamer->pos  = 0;
    streamer->disk = disk;
    return streamer;
}

// Lecture 83 - alternative constructor for callers that already
// hold the disk pointer (FAT16 in particular, after the L78/L82
// virtual-disk wiring).
struct disk_stream* diskstreamer_new_from_disk(struct disk* disk){
    if(!disk){
        return 0;
    }
    struct disk_stream* streamer = kzalloc(sizeof(struct disk_stream));
    streamer->pos  = 0;
    streamer->disk = disk;
    return streamer;
}

int diskstreamer_seek(struct disk_stream* stream, int pos){
    stream->pos = pos;
    return 0;
}

// Lecture 64 - rewritten iteratively.
//
// The original recursive form had two problems:
//   1. It split the read at the FIRST sector boundary, then
//      recursed for the remainder. Cross-sector reads stacked
//      one C call per sector - 4 KB of read = 8 stack frames
//      and could easily blow the kernel stack for large files.
//   2. The `total_to_read = (offset + total) - SAMOS_SECTOR_SIZE`
//      calculation for the overflow case was correct but easy
//      to get wrong on review.
//
// The new loop computes a chunk that fits in the current
// sector, reads it, advances out/pos/remaining, and repeats
// until remaining hits 0. No recursion, no stack risk.
int diskstreamer_read(struct disk_stream* stream, void* out, int total){
    if(total <= 0){
        return -1;
    }

    char* outc = out;
    int   remaining = total;

    while(remaining > 0){
        int  sector = stream->pos / SAMOS_SECTOR_SIZE;
        int  offset = stream->pos % SAMOS_SECTOR_SIZE;
        int  chunk  = SAMOS_SECTOR_SIZE - offset;
        if(chunk > remaining){
            chunk = remaining;
        }

        char buf[SAMOS_SECTOR_SIZE];
        int res = disk_read_block(stream->disk, sector, 1, buf);
        if(res < 0){
            return res;
        }

        memcpy(outc, buf + offset, chunk);
        outc         += chunk;
        stream->pos  += chunk;
        remaining    -= chunk;
    }
    return 0;
}

void diskstreamer_close(struct disk_stream* stream){
    kfree(stream);
}
