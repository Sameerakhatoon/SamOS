#ifndef DISKSTREAMER_H
#define DISKSTREAMER_H

#include "disk.h"

struct disk_stream {
    int           pos;
    struct disk*  disk;
};

struct disk_stream* diskstreamer_new(int disk_id);
// Lecture 83 - bind a streamer directly to an already-resolved
// disk pointer (e.g. a GPT partition virtual disk). Skips the
// disk_get id lookup, which would not work for virtual disks
// the user code did not look up by index.
struct disk_stream* diskstreamer_new_from_disk(struct disk* disk);
int                 diskstreamer_seek(struct disk_stream* stream, int pos);
int                 diskstreamer_read(struct disk_stream* stream, void* out, int total);
void                diskstreamer_close(struct disk_stream* stream);

#endif
