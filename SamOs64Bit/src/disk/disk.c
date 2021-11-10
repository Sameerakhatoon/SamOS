// Lecture 78 - disk layer restructured around a vector of disks.
//
// Old: fixed `struct disk disk_array[4]` with one entry.
// New: dynamic `struct vector* disk_vector` of struct disk*.
//
// disk_create_new pushes a new disk onto the vector. Three callers:
//   1. disk_search_and_init: creates one TYPE_REAL disk for the
//      physical ATA primary.
//   2. gpt_mount_partitions: creates one TYPE_PARTITION per GPT
//      entry.
//   3. (future: anything else that wants a virtual disk view)
//
// Each disk has starting_lba / ending_lba bounds. disk_read_block
// adds the starting LBA so virtual disks see their own
// zero-based LBA space, and rejects reads beyond ending_lba.
// The primary disk uses zero/zero to skip bounds checks entirely.
//
// SamOs DEVIATION: this file depends on the vector library
// (which itself depends on L80's krealloc) and the filesystem
// volume_name vtable entry (L84). Not in the build at L78.

#include "disk.h"
#include "io/io.h"
#include "config.h"
#include "status.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "string/string.h"
#include "lib/vector/vector.h"

struct vector* disk_vector = NULL;

// Pointer to the primary hard disk (LBA 0..end, no bounds check).
struct disk* disk_primary_handle = NULL;

// Lecture 78 - pointer to the virtual disk whose volume name
// matches SAMOS_KERNEL_FILESYSTEM_NAME. This is the disk where
// the kernel finds its own files (kernel.bin, blank.elf, etc).
struct disk* primary_fs_disk = NULL;

int disk_read_sector(int lba, int total, void* buf){
    while(insb(0x1F7) & 0x80){}

    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, (unsigned char)total);
    outb(0x1F3, (unsigned char)(lba & 0xff));
    outb(0x1F4, (unsigned char)((lba >> 8) & 0xff));
    outb(0x1F5, (unsigned char)((lba >> 16) & 0xff));
    outb(0x1F7, 0x20);

    unsigned short* ptr = (unsigned short*)buf;
    for(int b = 0; b < total; b++){
        while(insb(0x1F7) & 0x80){}
        char status = insb(0x1F7);
        if(status & 0x01){
            return -EIO;
        }
        while(!(insb(0x1F7) & 0x08)){}
        for(int word = 0; word < 256; word++){
            *ptr++ = insw(0x1F0);
        }
    }
    return 0;
}

int disk_create_new(int type, int starting_lba, int ending_lba,
                    size_t sector_size, struct disk** disk_out){
    int res = 0;
    struct disk* disk = kzalloc(sizeof(struct disk));
    if(!disk){
        res = -ENOMEM;
        goto out;
    }
    disk->type         = type;
    disk->id           = vector_count(disk_vector);
    disk->sector_size  = sector_size;
    disk->starting_lba = starting_lba;
    disk->ending_lba   = ending_lba;

    // Try to attach a filesystem driver. Not having one is
    // not an error - some disks (raw scratch partitions) don't
    // hold filesystems.
    disk->filesystem = fs_resolve(disk);
    if(disk->filesystem){
        char fs_name[11] = {0};
        char primary_drive_fs_name[11] = {0};
        strncpy(primary_drive_fs_name, SAMOS_KERNEL_FILESYSTEM_NAME,
                strlen(SAMOS_KERNEL_FILESYSTEM_NAME));
        // L78 NOTE: upstream calls
        //   disk->filesystem->volume_name(disk->fs_private,
        //                                 fs_name, sizeof(fs_name));
        // here, but the volume_name vtable slot is added in L84.
        // Compiling against L78 with this call active fails -
        // SamOs guards it with `#if 0` until L84 lands. The
        // primary_fs_disk selection therefore degenerates to
        // "first disk wins" until then.
        // L84-stub TODO: re-enable the volume_name comparison.
#if 0
        disk->filesystem->volume_name(disk->fs_private, fs_name, sizeof(fs_name));
        if(strncmp(fs_name, primary_drive_fs_name, sizeof(fs_name)) == 0){
            primary_fs_disk = disk;
        }
#else
        if(primary_fs_disk == NULL){
            primary_fs_disk = disk;
        }
        (void)primary_drive_fs_name;
        (void)fs_name;
#endif
    }

    if(disk_out){
        *disk_out = disk;
    }
    vector_push(disk_vector, &disk);
out:
    return res;
}

void disk_search_and_init(){
    int res = 0;
    disk_vector = vector_new(sizeof(struct disk*), 4, 0);
    if(!disk_vector){
        res = -ENOMEM;
        goto out;
    }
    res = disk_create_new(SAMOS_DISK_TYPE_REAL, 0, 0, SAMOS_SECTOR_SIZE,
                          &disk_primary_handle);
    if(res < 0){
        goto out;
    }
out:
    return;
}

struct disk* disk_primary(void){
    return disk_primary_handle;
}

struct disk* disk_primary_fs_disk(void){
    return primary_fs_disk;
}

struct disk* disk_get(int index){
    size_t total_disks = vector_count(disk_vector);
    if(index >= (int)total_disks){
        return NULL;
    }
    struct disk* disk = NULL;
    vector_at(disk_vector, index, &disk, sizeof(disk));
    return disk;
}

int disk_read_block(struct disk* idisk, unsigned int lba, int total, void* buf){
    size_t absolute_lba        = idisk->starting_lba + lba;
    size_t absolute_ending_lba = absolute_lba + total;
    if(absolute_ending_lba > idisk->ending_lba){
        // Bypass bounds check for the primary disk.
        if(idisk->starting_lba != 0 && idisk->ending_lba != 0){
            return -EIO;
        }
    }
    return disk_read_sector(absolute_lba, total, buf);
}
