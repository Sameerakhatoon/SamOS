#include "streamer.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"   // L64 - memcpy
#include "config.h"
#include "kernel.h"          // L204 - panic + krealloc
#include "status.h"          // L204 - status codes
#include <stdbool.h>

// Lecture 204 - per-disk stream cache. The cache is a sparse
// three-level lookup tree (level1 -> level2 -> level3 of cache
// sectors) plus a round-robin eviction queue. L205 lands the
// path-walk + lookup helpers, L206/L207 wire it into read.

struct disk_stream_cache* diskstreamer_cache_new(){
    struct disk_stream_cache* cache = kzalloc(sizeof(struct disk_stream_cache));
    if(!cache){
        goto out;
    }
out:
    return cache;
}

// Lecture 204 - lazily grow the level-1 array to `index + 1`
// and return the bucket at `index`. Upstream uses
// `sizeof(struct disk_Stream_cache_bucket_level1**)` (capital
// S - struct typo). gcc accepts the typo as a forward decl of
// an unrelated struct; sizeof of pointer-to-pointer-to-struct
// is sizeof(void*) so the byte-count math accidentally works.
// Preserved verbatim per the project rule.
struct disk_stream_cache_bucket_level1* diskstreamer_cache_bucket_level1_get(
    struct disk_stream_cache* cache, int index){
    struct disk_stream_cache_bucket_level1* level1 = NULL;
    if((int)cache->total <= index){
        size_t old_total = cache->total;
        size_t new_total = index + 1;
        size_t new_size = new_total * sizeof(struct disk_Stream_cache_bucket_level1**);

        struct disk_stream_cache_bucket_level1** new_buckets =
            krealloc(cache->buckets, new_size);
        if(!new_buckets){
            return NULL;
        }
        memset(new_buckets + old_total, 0,
               (new_total - old_total) * sizeof(*new_buckets));
        cache->buckets = new_buckets;
        cache->total   = new_total;
    }

    level1 = cache->buckets[index];
    if(!level1){
        cache->buckets[index] = kzalloc(sizeof(struct disk_stream_cache_bucket_level1));
        if(!cache->buckets[index]){
            return NULL;
        }
        level1 = cache->buckets[index];
        level1->total_buckets = 0;
    }
    return level1;
}

struct disk_stream_cache_bucket_level2* diskstreamer_cache_bucket_level2_get(
    struct disk_stream_cache_bucket_level1* level1_bucket, int index){
    int max_buckets = (sizeof(level1_bucket->buckets) / sizeof(*level1_bucket->buckets));
    if(index >= max_buckets){
        return NULL;
    }
    struct disk_stream_cache_bucket_level2* level_two = level1_bucket->buckets[index];
    if(!level_two){
        level_two = kzalloc(sizeof(struct disk_stream_cache_bucket_level2));
        if(!level_two){
            return NULL;
        }
        level_two->total_buckets = 0;
        level1_bucket->buckets[index] = level_two;
    }
    return level_two;
}

struct disk_stream_cache_bucket_level3* diskstreamer_cache_bucket_level3_get(
    struct disk_stream_cache_bucket_level2* level2, int index){
    struct disk_stream_cache_bucket_level3* level3 = NULL;
    int max_buckets = (sizeof(level2->buckets) / sizeof(*level2->buckets));
    if(index >= max_buckets){
        return NULL;
    }
    level3 = level2->buckets[index];
    if(!level3){
        level3 = kzalloc(sizeof(struct disk_stream_cache_bucket_level3));
        if(!level3){
            return NULL;
        }
        level3->total_sectors = 0;
        level2->buckets[index] = level3;
    }
    return level3;
}

void diskstreamer_cache_bucket_level3_free(struct disk_stream_cache_bucket_level3* level3){
    for(size_t i = 0; i < DISK_STREAM_LEVEL3_SECTORS_ARRAY_SIZE; i++){
        kfree(level3->sectors[i]);
    }
    kfree(level3);
}

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
