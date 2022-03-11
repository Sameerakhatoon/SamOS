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
#include "disk/streamer.h"     // L206 - diskstreamer_cache_new

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

// Lecture 183 - return the hardware-disk back-pointer.
struct disk* disk_hardware_disk(struct disk* disk){
    return disk->hardware_disk;
}

// Lecture 188 - per-disk driver private accessor. Upstream's
// decl lands at L189; SamOs lands the body now so the L188
// PATA driver source links.
void* disk_private_data_driver(struct disk* disk){
    return disk->driver_private;
}

// Lecture 206 - absolute-LBA + byte-offset helpers used by the
// stream cache lookup.
long disk_real_sector(struct disk* idisk, unsigned int lba){
    size_t absolute_lba = idisk->starting_lba + lba;
    return absolute_lba;
}

long disk_real_offset(struct disk* idisk, unsigned int lba){
    size_t absolute_lba = disk_real_sector(idisk, lba);
    return absolute_lba * idisk->sector_size;
}

// Lecture 186 - delegate partition creation to the disk's
// driver. The driver vtable is responsible for spinning up the
// virtual disk that maps starting_lba..ending_lba.
int disk_create_partition(struct disk* disk, int starting_lba, int ending_lba,
                          struct disk** partition_disk_out){
    return disk_driver_mount_partition(disk->driver, disk, starting_lba, ending_lba,
                                       partition_disk_out);
}

// Lecture 186 - extracted FS-mount step. disk_create_new used
// to inline this; L186 splits it so callers can choose when to
// run fs_resolve relative to driver-side mount work.
int disk_filesystem_mount(struct disk* disk){
    disk->filesystem = fs_resolve(disk);
    if(disk->filesystem){
        char fs_name[11] = {0};
        char primary_drive_fs_name[11] = {0};
        strncpy(primary_drive_fs_name, SAMOS_KERNEL_FILESYSTEM_NAME,
                strlen(SAMOS_KERNEL_FILESYSTEM_NAME));
        if(disk->filesystem->volume_name){
            disk->filesystem->volume_name(disk->fs_private, fs_name, sizeof(fs_name));
            if(strncmp(fs_name, primary_drive_fs_name, sizeof(fs_name)) == 0){
                primary_fs_disk = disk;
            }
        }
    }
    return 0;
}

int disk_create_new(struct disk_driver* driver,
                    struct disk* hardware_disk,
                    int type, int starting_lba, int ending_lba,
                    size_t sector_size, void* driver_private_data,
                    struct disk** disk_out){
    int res = 0;
    struct disk* disk = kzalloc(sizeof(struct disk));
    if(!disk){
        res = -ENOMEM;
        goto out;
    }

    // Lecture 183 - REAL disks own themselves; passing in a
    // separate hardware_disk for a REAL disk is illegal. A
    // partition disk must point at a REAL backing disk.
    if(hardware_disk && type == SAMOS_DISK_TYPE_REAL){
        res = -EINVARG;
        goto out;
    }
    if(type == SAMOS_DISK_TYPE_REAL){
        hardware_disk = disk;
    }
    if(hardware_disk == NULL){
        res = -EINVARG;
        goto out;
    }
    if(hardware_disk->type != SAMOS_DISK_TYPE_REAL){
        res = -EINVARG;
        goto out;
    }

    disk->type           = type;
    disk->id             = vector_count(disk_vector);
    disk->sector_size    = sector_size;
    disk->starting_lba   = starting_lba;
    disk->ending_lba     = ending_lba;
    disk->driver         = driver;
    disk->driver_private = driver_private_data;
    disk->hardware_disk  = hardware_disk;
    // Lecture 206 - allocate the per-disk stream cache eagerly.
    disk->cache          = diskstreamer_cache_new();

    // Lecture 186 - filesystem mount moves out of this function
    // into disk_filesystem_mount. Callers run it after the
    // driver-side mount work completes. SamOs DEVIATION: the
    // bootstrap caller still has no driver registered (L189
    // wires PATA), so we run fs_resolve inline when the disk
    // arrived without a driver to keep the kernel boot path
    // alive until then. Upstream drops the inline call and
    // crashes during early boot, since fs_resolve is normally
    // called from disk_filesystem_mount that the L189 PATA
    // bootstrap will invoke.
    if(!driver){
        disk_filesystem_mount(disk);
        if(primary_fs_disk == NULL){
            primary_fs_disk = disk;
        }
    }

    if(disk_out){
        *disk_out = disk;
    }
    vector_push(disk_vector, &disk);
out:
    return res;
}

// Lecture 189 - run every registered disk driver's mount op.
int disk_mount_all(){
    int res = 0;
    res = disk_driver_mount_all();
    return res;
}

// Lecture 189 - boot-time disk init now drives the driver
// registry: disk_driver_system_init populates the registry
// (which inserts the PATA driver), allocate the disk vector,
// then mount-all. The legacy disk_create_new for disk0 is
// gone; PATA's mount handler creates the REAL disk itself.
int disk_search_and_init(){
    int res = 0;

    res = disk_driver_system_init();
    if(res < 0){
        res = -EIO;
        goto out;
    }

    disk_vector = vector_new(sizeof(struct disk*), 4, 0);
    if(!disk_vector){
        res = -ENOMEM;
        goto out;
    }

    res = disk_mount_all();
    if(res < 0){
        goto out;
    }

    // SamOs DEVIATION: cache disk_primary_handle so disk_primary()
    // keeps working. PATA's mount inserts the new REAL disk at
    // index 0; we read it back.
    disk_primary_handle = disk_get(0);
out:
    return res;
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
    // Lecture 186 - route through the disk-driver vtable when
    // a driver is registered. SamOs DEVIATION: fall back to the
    // direct ATA path when the disk predates L189's PATA driver
    // registration so the bootstrap disk0 keeps working.
    if(idisk->driver){
        if(!idisk->driver->functions.read){
            return -EIO;
        }
        return idisk->driver->functions.read(idisk, absolute_lba, total, buf);
    }
    return disk_read_sector(absolute_lba, total, buf);
}
