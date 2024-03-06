// RAM disk: one REAL disk over an image in memory, for the
// ports before they have a disk controller (the aarch64 device work brings virtio-blk
// over MMIO). The image is reached through the direct map; the PFA
// never hands its frames out (the arch reserves them in the memory
// map). Reads and writes are copies; the disk is writable, it is RAM.

#include "ramdisk.h"
#include "disk/disk.h"
#include "disk/driver.h"
#include "memory/memory.h"
#include "arch/arch.h"
#include "io/serial.h"
#include "string/string.h"
#include "status.h"

#define RAMDISK_SECTOR_SIZE 512

static uint8_t* ramdisk_base;
static uint64_t ramdisk_sectors;

void ramdisk_attach(uint64_t phys, uint64_t size){
    ramdisk_base    = (uint8_t*)PAGING_PHYS_TO_VIRT(phys);
    ramdisk_sectors = size / RAMDISK_SECTOR_SIZE;
}

static int ramdisk_read(struct disk* disk, uint64_t lba, int total, void* buf){
    if(lba >= ramdisk_sectors || (uint64_t)total > ramdisk_sectors - lba){
        return -EIO;
    }
    memcpy(buf, ramdisk_base + lba * RAMDISK_SECTOR_SIZE, total * RAMDISK_SECTOR_SIZE);
    return 0;
}

static int ramdisk_write(struct disk* disk, uint64_t lba, int total, void* buf){
    if(lba >= ramdisk_sectors || (uint64_t)total > ramdisk_sectors - lba){
        return -EIO;
    }
    memcpy(ramdisk_base + lba * RAMDISK_SECTOR_SIZE, buf, total * RAMDISK_SECTOR_SIZE);
    return 0;
}

static int ramdisk_mount(struct disk_driver* driver){
    struct disk* disk = NULL;
    if(!ramdisk_sectors){
        return 0;
    }
    int res = disk_create_new(driver, NULL, SAMOS_DISK_TYPE_REAL, 0, ramdisk_sectors,
                              RAMDISK_SECTOR_SIZE, NULL, &disk);
    if(res < 0){
        return res;
    }
    disk->read_only = false;
    serial_write("ramdisk: disk ");
    serial_write(itoa(disk->id));
    serial_write(", ");
    serial_write(itoa((int)(ramdisk_sectors / 2)));
    serial_write(" KiB\n");
    return 0;
}

static struct disk_driver ramdisk_driver = {
    .name = {"RAMDISK"},
    .functions.read  = ramdisk_read,
    .functions.write = ramdisk_write,
    .functions.mount = ramdisk_mount,
};

struct disk_driver* ramdisk_driver_init(void){
    return &ramdisk_driver;
}
