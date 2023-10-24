// Lecture 83 / 203-207 - disk streamer + sector cache.
// The storage work rewrite; see streamer.h for why the course's cache tree
// was replaced.

#include "streamer.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "config.h"
#include "kernel.h"
#include "status.h"

// Runs of at least this many whole sectors bypass the cache on
// read and go straight to the driver (one multi-sector command
// instead of one command per sector). Safe because the cache is
// write-through: the disk always holds the newest data.
#define DISK_STREAM_DIRECT_RUN 8

struct disk_stream_cache* diskstreamer_cache_new(){
    return kzalloc(sizeof(struct disk_stream_cache));
}

static struct disk_stream_cache_slot* diskstreamer_cache_slot(struct disk_stream_cache* cache,
                                                             uint64_t abs_lba){
    return &cache->slots[abs_lba % DISK_STREAM_CACHE_SLOTS];
}

void diskstreamer_cache_update(struct disk* hw, uint64_t abs_lba, int total,
                               const void* data, bool ok){
    struct disk_stream_cache* cache = hw ? hw->cache : NULL;
    if(!cache){
        return;
    }
    for(int i = 0; i < total; i++){
        struct disk_stream_cache_slot* slot = diskstreamer_cache_slot(cache, abs_lba + i);
        if(!slot->valid || slot->lba != abs_lba + i){
            continue;
        }
        if(ok){
            memcpy(slot->buf, (char*)data + (size_t)i * hw->sector_size, hw->sector_size);
        } else {
            slot->valid = false;
        }
    }
}

// Return the cached copy of one sector of the view, reading it
// from the disk on a miss.
static int diskstreamer_cache_get(struct disk* disk, uint64_t lba, char** buf_out){
    struct disk_stream_cache* cache = disk->cache;
    uint64_t abs_lba = disk_real_sector(disk, lba);
    if(!cache){
        return -EIO;
    }
    struct disk_stream_cache_slot* slot = diskstreamer_cache_slot(cache, abs_lba);
    if(slot->valid && slot->lba == abs_lba){
        cache->hits++;
        *buf_out = slot->buf;
        return 0;
    }
    cache->misses++;
    if(!slot->buf){
        slot->buf = kzalloc(DISK_STREAMER_MAX_CACHE_SECTOR_SIZE);
        if(!slot->buf){
            return -ENOMEM;
        }
    }
    slot->valid = false;
    int res = disk_read_block(disk, lba, 1, slot->buf);
    if(res < 0){
        return res;
    }
    slot->lba   = abs_lba;
    slot->valid = true;
    *buf_out    = slot->buf;
    return 0;
}

// A direct run is served from the cache when every sector of
// it is there, and fills the cache after it was read. Without this
// every load of the same program read the whole file over PIO again
// (36 KiB of BLANK.ELF took 26 ms under TCG, and the memory manager process
// leak test loads it 1000 times).
static bool diskstreamer_cache_run(struct disk* disk, uint64_t lba, int sectors, char* out){
    struct disk_stream_cache* cache = disk->cache;
    int64_t ss = disk->sector_size;
    if(!cache || ss > DISK_STREAMER_MAX_CACHE_SECTOR_SIZE){
        return false;
    }
    for(int i = 0; i < sectors; i++){
        uint64_t abs_lba = disk_real_sector(disk, lba + i);
        struct disk_stream_cache_slot* slot = diskstreamer_cache_slot(cache, abs_lba);
        if(!slot->valid || slot->lba != abs_lba){
            return false;
        }
    }
    for(int i = 0; i < sectors; i++){
        uint64_t abs_lba = disk_real_sector(disk, lba + i);
        memcpy(out + i * ss, diskstreamer_cache_slot(cache, abs_lba)->buf, ss);
    }
    cache->hits += sectors;
    return true;
}

static void diskstreamer_cache_fill(struct disk* disk, uint64_t lba, int sectors, const char* data){
    struct disk_stream_cache* cache = disk->cache;
    int64_t ss = disk->sector_size;
    if(!cache || ss > DISK_STREAMER_MAX_CACHE_SECTOR_SIZE){
        return;
    }
    for(int i = 0; i < sectors; i++){
        uint64_t abs_lba = disk_real_sector(disk, lba + i);
        struct disk_stream_cache_slot* slot = diskstreamer_cache_slot(cache, abs_lba);
        if(!slot->buf){
            slot->buf = kzalloc(DISK_STREAMER_MAX_CACHE_SECTOR_SIZE);
            if(!slot->buf){
                return;
            }
        }
        memcpy(slot->buf, (char*)data + i * ss, ss);
        slot->lba   = abs_lba;
        slot->valid = true;
    }
}

struct disk_stream* diskstreamer_new(int disk_id){
    return diskstreamer_new_from_disk(disk_get(disk_id));
}

struct disk_stream* diskstreamer_new_from_disk(struct disk* disk){
    if(!disk){
        return 0;
    }
    struct disk_stream* streamer = kzalloc(sizeof(struct disk_stream));
    if(!streamer){
        return 0;
    }
    streamer->pos         = 0;
    streamer->sector_size = disk->sector_size;
    streamer->disk        = disk;
    return streamer;
}

int diskstreamer_seek(struct disk_stream* stream, int64_t pos){
    if(pos < 0){
        return -EINVARG;
    }
    stream->pos = pos;
    return 0;
}

int diskstreamer_read(struct disk_stream* stream, void* out, int total){
    char* dst = out;
    int64_t ss = stream->sector_size;
    if(total < 0){
        return -EINVARG;
    }
    while(total > 0){
        uint64_t lba    = stream->pos / ss;
        int      offset = stream->pos % ss;

        if(offset == 0 && total >= ss * DISK_STREAM_DIRECT_RUN){
            int sectors = total / ss;
            if(sectors > 128){
                sectors = 128;
            }
            if(!diskstreamer_cache_run(stream->disk, lba, sectors, dst)){
                int res = disk_read_block(stream->disk, lba, sectors, dst);
                if(res < 0){
                    return res;
                }
                diskstreamer_cache_fill(stream->disk, lba, sectors, dst);
            }
            dst         += sectors * ss;
            stream->pos += sectors * ss;
            total       -= sectors * ss;
            continue;
        }

        char* sector = NULL;
        int res = diskstreamer_cache_get(stream->disk, lba, &sector);
        if(res < 0){
            return res;
        }
        int chunk = ss - offset;
        if(chunk > total){
            chunk = total;
        }
        memcpy(dst, sector + offset, chunk);
        dst         += chunk;
        stream->pos += chunk;
        total       -= chunk;
    }
    return 0;
}

int diskstreamer_write(struct disk_stream* stream, const void* in, int total){
    const char* src = in;
    int64_t ss = stream->sector_size;
    if(total < 0){
        return -EINVARG;
    }
    if(stream->disk->read_only){
        return -ERDONLY;
    }
    while(total > 0){
        uint64_t lba    = stream->pos / ss;
        int      offset = stream->pos % ss;

        if(offset == 0 && total >= ss){
            // Whole sectors: write straight from the caller's
            // buffer. disk_write_block refreshes the cache.
            int sectors = total / ss;
            if(sectors > 128){
                sectors = 128;
            }
            int res = disk_write_block(stream->disk, lba, sectors, src);
            if(res < 0){
                return res;
            }
            src         += sectors * ss;
            stream->pos += sectors * ss;
            total       -= sectors * ss;
            continue;
        }

        // Partial sector: read-modify-write through the cache.
        char* sector = NULL;
        int res = diskstreamer_cache_get(stream->disk, lba, &sector);
        if(res < 0){
            return res;
        }
        int chunk = ss - offset;
        if(chunk > total){
            chunk = total;
        }
        char tmp[ss];
        memcpy(tmp, sector, ss);
        memcpy(tmp + offset, (void*)src, chunk);
        res = disk_write_block(stream->disk, lba, 1, tmp);
        if(res < 0){
            return res;
        }
        src         += chunk;
        stream->pos += chunk;
        total       -= chunk;
    }
    return 0;
}

void diskstreamer_close(struct disk_stream* stream){
    kfree(stream);
}
