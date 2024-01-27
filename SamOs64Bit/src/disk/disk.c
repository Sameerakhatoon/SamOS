// Lecture 78 - disk layer restructured around a vector of disks.
//
// disk_create_new pushes a new disk onto the vector. Callers:
//   1. disk drivers (PATA, AHCI, NVMe) create one TYPE_REAL disk
//      per physical drive from their mount op.
//   2. the partition scanners (gpt.c, mbr.c) create one
//      TYPE_PARTITION disk per partition.
//
// Rework:
//   - LBAs and sizes are 64-bit everywhere (the old int/unsigned
//     int signatures capped disks at 2 GiB of byte offset).
//   - Every read and write goes through the HARDWARE disk's
//     driver with an absolute LBA. The old driverless fallback
//     that hard-coded the primary IDE ports at 0x1F0 is gone, so
//     NVMe and AHCI partitions work.
//   - disk_search_and_init scans every REAL disk, not just
//     disk 0: GPT first, then a filesystem on the whole disk
//     ("superfloppy"), then an MBR partition table.
//   - Writes exist, and obey the read-only policy in
//     storage_policy.h.

#include "disk.h"
#include "gpt.h"
#include "mbr.h"
#include "storage_policy.h"
#include "arch/arch.h"
#include "config.h"
#include "status.h"
#include "kernel.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "string/string.h"
#include "lib/vector/vector.h"
#include "disk/streamer.h"     // L206 - diskstreamer_cache_new

struct vector* disk_vector = NULL;

// One CPU at a time in any disk driver, for every disk:
// two PATA drives share a channel's registers and its interrupt, the
// AHCI ports of one HBA share its interrupt line and wait flag, and
// a disk wait steers that line to its own CPU. Recursive, so
// the streamer holds it over a cache fill through disk_read_block.
struct spinlock disk_lock = SPINLOCK_INIT("disk", LOCK_LEVEL_DISK, true);

// Pointer to the first REAL disk.
struct disk* disk_primary_handle = NULL;

// Lecture 78 - the disk whose volume name matches
// SAMOS_KERNEL_FILESYSTEM_NAME. The kernel finds its own files
// there ("@:/"). Falls back to the first disk with any
// filesystem.
struct disk* primary_fs_disk = NULL;
static struct disk* first_fs_disk = NULL;

bool disk_running_on_hypervisor(void){
    return arch_on_hypervisor();
}

// The read-only policy for a new REAL disk. See
// storage_policy.h.
static bool disk_policy_read_only(void){
    if(SAMOS_STORAGE_FORCE_READ_ONLY){
        return true;
    }
    if(SAMOS_STORAGE_WRITE_REAL_HW){
        return false;
    }
    return !disk_running_on_hypervisor();
}

// Lecture 183 - return the hardware-disk back-pointer.
struct disk* disk_hardware_disk(struct disk* disk){
    return disk->hardware_disk;
}

void* disk_private_data_driver(struct disk* disk){
    return disk->driver_private;
}

uint64_t disk_real_sector(struct disk* idisk, uint64_t lba){
    return idisk->starting_lba + lba;
}

uint64_t disk_real_offset(struct disk* idisk, uint64_t lba){
    return disk_real_sector(idisk, lba) * (uint64_t)idisk->sector_size;
}

int disk_create_partition(struct disk* disk, uint64_t starting_lba, uint64_t ending_lba,
                          struct disk** partition_disk_out){
    if(!disk || disk->type != SAMOS_DISK_TYPE_REAL || ending_lba < starting_lba){
        return -EINVARG;
    }
    if(disk->total_sectors && ending_lba >= disk->total_sectors){
        return -EOUTOFRANGE;
    }
    return disk_create_new(disk->driver, disk, SAMOS_DISK_TYPE_PARTITION,
                           starting_lba, ending_lba - starting_lba + 1,
                           disk->sector_size, disk->driver_private,
                           partition_disk_out);
}

// Lecture 186 - run every registered filesystem's resolve over
// the disk. Returns 0 when one claimed it.
int disk_filesystem_mount(struct disk* disk){
    disk->filesystem = fs_resolve(disk);
    if(!disk->filesystem){
        return -EFSNOTUS;
    }
    if(!first_fs_disk){
        first_fs_disk = disk;
    }
    if(disk->filesystem->volume_name && !primary_fs_disk){
        char fs_name[12] = {0};
        disk->filesystem->volume_name(disk->fs_private, fs_name, sizeof(fs_name));
        if(strncmp(fs_name, SAMOS_KERNEL_FILESYSTEM_NAME, 11) == 0){
            primary_fs_disk = disk;
        }
    }
    return 0;
}

int disk_create_new(struct disk_driver* driver,
                    struct disk* hardware_disk,
                    int type, uint64_t starting_lba, uint64_t total_sectors,
                    size_t sector_size, void* driver_private_data,
                    struct disk** disk_out){
    int res = 0;
    if(!disk_vector){
        return -EIO;
    }
    // Lecture 183 - REAL disks own themselves; a partition disk
    // must point at a REAL backing disk.
    if(hardware_disk && type == SAMOS_DISK_TYPE_REAL){
        return -EINVARG;
    }
    if(type != SAMOS_DISK_TYPE_REAL &&
       (!hardware_disk || hardware_disk->type != SAMOS_DISK_TYPE_REAL)){
        return -EINVARG;
    }
    if(sector_size == 0 || sector_size > 4096){
        return -EINVARG;
    }

    struct disk* disk = kzalloc(sizeof(struct disk));
    if(!disk){
        return -ENOMEM;
    }
    if(type == SAMOS_DISK_TYPE_REAL){
        hardware_disk  = disk;
        disk->cache    = diskstreamer_cache_new();
        disk->read_only = disk_policy_read_only();
    } else {
        disk->cache     = hardware_disk->cache;
        disk->read_only = hardware_disk->read_only;
    }

    disk->type           = type;
    disk->id             = vector_count(disk_vector);
    disk->sector_size    = sector_size;
    disk->starting_lba   = starting_lba;
    disk->total_sectors  = total_sectors;
    disk->ending_lba     = total_sectors ? starting_lba + total_sectors - 1 : 0;
    disk->driver         = driver;
    disk->driver_private = driver_private_data;
    disk->hardware_disk  = hardware_disk;

    // A disk that could not be registered is freed here; the
    // caller frees its own private data on any error.
    res = vector_push(disk_vector, &disk);
    if(res < 0){
        if(type == SAMOS_DISK_TYPE_REAL){
            kfree(disk->cache);
        }
        kfree(disk);
        return res;
    }
    if(disk_out){
        *disk_out = disk;
    }
    return 0;
}

int disk_mount_all(){
    return disk_driver_mount_all();
}

// Find the filesystems on one REAL disk.
static void disk_scan_one(struct disk* disk){
    int partitions = gpt_scan_disk(disk);
    if(partitions > 0){
        return;
    }
    if(disk_filesystem_mount(disk) == 0){
        return;
    }
    mbr_scan_disk(disk);
}

int disk_search_and_init(){
    int res = 0;

    disk_vector = vector_new(sizeof(struct disk*), 4, 0);
    if(!disk_vector){
        return -ENOMEM;
    }

    res = disk_driver_system_init();
    if(res < 0){
        res = -EIO;
        goto out;
    }

    disk_mount_all();

    // Only the REAL disks the drivers created; the scanners
    // append partition disks behind them.
    int total = disk_count();
    for(int i = 0; i < total; i++){
        struct disk* disk = disk_get(i);
        if(disk && disk->type == SAMOS_DISK_TYPE_REAL){
            disk_scan_one(disk);
        }
    }
    // Partition disks were created by the scan; mount theirs.
    total = disk_count();
    for(int i = 0; i < total; i++){
        struct disk* disk = disk_get(i);
        if(disk && disk->type == SAMOS_DISK_TYPE_PARTITION && !disk->filesystem){
            disk_filesystem_mount(disk);
        }
    }

    if(!primary_fs_disk){
        primary_fs_disk = first_fs_disk;
    }
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

int disk_count(void){
    return disk_vector ? (int)vector_count(disk_vector) : 0;
}

struct disk* disk_get(int index){
    if(index < 0 || index >= disk_count()){
        return NULL;
    }
    struct disk* disk = NULL;
    vector_at(disk_vector, index, &disk, sizeof(disk));
    return disk;
}

static int disk_check_range(struct disk* idisk, uint64_t lba, int total){
    if(total <= 0){
        return -EINVARG;
    }
    if(idisk->total_sectors &&
       (lba >= idisk->total_sectors || (uint64_t)total > idisk->total_sectors - lba)){
        return -EIO;
    }
    struct disk* hw = idisk->hardware_disk;
    if(!hw || !hw->driver){
        return -EIO;
    }
    return 0;
}

int disk_read_block(struct disk* idisk, uint64_t lba, int total, void* buf){
    int res = disk_check_range(idisk, lba, total);
    if(res < 0){
        return res;
    }
    struct disk* hw = idisk->hardware_disk;
    if(!hw->driver->functions.read){
        return -EIO;
    }
    // Drivers are entered by one CPU at a time.
    spin_lock(&disk_lock);
    res = hw->driver->functions.read(hw, disk_real_sector(idisk, lba), total, buf);
    spin_unlock(&disk_lock);
    return res;
}

int disk_write_block(struct disk* idisk, uint64_t lba, int total, const void* buf){
    int res = disk_check_range(idisk, lba, total);
    if(res < 0){
        return res;
    }
    if(idisk->read_only){
        return -ERDONLY;
    }
    struct disk* hw = idisk->hardware_disk;
    if(!hw->driver->functions.write){
        return -ERDONLY;
    }
    uint64_t abs_lba = disk_real_sector(idisk, lba);
    spin_lock(&disk_lock);
    res = hw->driver->functions.write(hw, abs_lba, total, (void*)buf);
    // Keep the sector cache coherent with direct block writes.
    diskstreamer_cache_update(hw, abs_lba, total, buf, res == 0);
    spin_unlock(&disk_lock);
    return res;
}
