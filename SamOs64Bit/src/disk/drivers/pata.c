// Lecture 188 - PATA (parallel ATA / IDE) disk driver.
//
// Rewrite. The course driver mounted the primary master
// only, never sent IDENTIFY (so the size was unknown), used
// LBA28 only, wrapped requests over 255 sectors (the count went
// into one byte), could not write, and had no timeouts. Now:
//   - all four drives are probed (primary/secondary channel,
//     master/slave), with native-mode PCI I/O ports when the
//     controller uses them;
//   - IDENTIFY gives the sector count and LBA48 support;
//   - requests are split into PATA_MAX_SECTORS_PER_CMD chunks,
//     LBA48 commands are used when the LBA needs them;
//   - writes are supported and followed by a cache flush;
//   - every wait has a timeout and checks ERR/DF.
// Interrupts are disabled on the device (nIEN); it is polled.

#include "pata.h"
#include "status.h"
#include "disk/disk.h"
#include "io/io.h"
#include "io/pci.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "kernel.h"
#include "disk/poll.h"


int pata_address(int base, int offset){
    return base + offset;
}

static uint8_t pata_status(struct pata_driver_private_data* d){
    return insb(d->io_base + PATA_REG_STATUS);
}

// Reading the alternate status register four times takes the
// 400 ns the spec wants after selecting a drive or sending a
// command, and does not clear a pending interrupt.
static void pata_delay400(struct pata_driver_private_data* d){
    for(int i = 0; i < 4; i++){
        insb(d->ctrl_base);
    }
}

static int pata_wait_not_busy(struct pata_driver_private_data* d){
    for(uint64_t end = disk_poll_deadline(); !disk_poll_expired(end); ){
        uint8_t st = pata_status(d);
        if(st == 0xFF){
            return -EIO;   // floating bus: nothing there
        }
        if(!(st & PATA_SR_BSY)){
            return 0;
        }
    }
    return -ETIMEOUT;
}

// Wait until the drive wants data (DRQ) or reports an error.
static int pata_wait_drq(struct pata_driver_private_data* d){
    for(uint64_t end = disk_poll_deadline(); !disk_poll_expired(end); ){
        uint8_t st = pata_status(d);
        if(st & PATA_SR_BSY){
            continue;
        }
        if(st & (PATA_SR_ERR | PATA_SR_DF)){
            return -EIO;
        }
        if(st & PATA_SR_DRQ){
            return 0;
        }
    }
    return -ETIMEOUT;
}

static void pata_select(struct pata_driver_private_data* d, uint8_t head_bits){
    outb(d->io_base + PATA_REG_DRIVE, head_bits | (d->slave ? 0x10 : 0x00));
    pata_delay400(d);
}

static void pata_ident_string(const uint16_t* id, int first_word, int words, char* out){
    for(int i = 0; i < words; i++){
        out[i * 2]     = (char)(id[first_word + i] >> 8);
        out[i * 2 + 1] = (char)(id[first_word + i] & 0xFF);
    }
    int len = words * 2;
    out[len] = 0;
    while(len > 0 && out[len - 1] == ' '){
        out[--len] = 0;
    }
}

// Send IDENTIFY DEVICE. Returns 0 and fills d when an ATA disk
// answers; ATAPI and SATA-bridge signatures are skipped.
static int pata_identify(struct pata_driver_private_data* d){
    uint16_t id[256];

    if(insb(d->io_base + PATA_REG_STATUS) == 0xFF){
        return -ENOENT;
    }
    outb(d->ctrl_base, 0x02);        // nIEN: no interrupts, we poll
    pata_select(d, 0xA0);
    outb(d->io_base + PATA_REG_SECCOUNT, 0);
    outb(d->io_base + PATA_REG_LBA0, 0);
    outb(d->io_base + PATA_REG_LBA1, 0);
    outb(d->io_base + PATA_REG_LBA2, 0);
    outb(d->io_base + PATA_REG_COMMAND, PATA_CMD_IDENTIFY);
    pata_delay400(d);

    if(pata_status(d) == 0){
        return -ENOENT;              // no drive on this position
    }
    if(pata_wait_not_busy(d) < 0){
        return -ENOENT;
    }
    if(insb(d->io_base + PATA_REG_LBA1) != 0 || insb(d->io_base + PATA_REG_LBA2) != 0){
        return -ENOENT;              // ATAPI (CD-ROM) or SATA signature
    }
    if(pata_wait_drq(d) < 0){
        return -ENOENT;
    }
    for(int i = 0; i < 256; i++){
        id[i] = insw(d->io_base + PATA_REG_DATA);
    }

    if(!(id[49] & (1 << 9))){
        return -EUNIMP;              // CHS-only drive
    }
    d->lba48 = (id[83] & (1 << 10)) != 0;
    uint64_t lba28 = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    uint64_t lba48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                     ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    d->total_sectors = (d->lba48 && lba48) ? lba48 : lba28;
    pata_ident_string(id, 27, 20, d->model);
    return d->total_sectors ? 0 : -EIO;
}

// Program the task file for one command of `count` sectors.
static int pata_setup(struct pata_driver_private_data* d, uint64_t lba, int count, bool* ext_out){
    bool ext = (lba + count > PATA_LBA28_LIMIT);
    if(ext && !d->lba48){
        return -EOUTOFRANGE;
    }
    // Select the drive first: the status register belongs to the
    // selected drive, so only then is the BSY wait meaningful.
    pata_select(d, ext ? 0x40 : (0xE0 | (uint8_t)((lba >> 24) & 0x0F)));
    int res = pata_wait_not_busy(d);
    if(res < 0){
        return res;
    }
    if(ext){
        outb(d->io_base + PATA_REG_SECCOUNT, (uint8_t)(count >> 8));
        outb(d->io_base + PATA_REG_LBA0, (uint8_t)(lba >> 24));
        outb(d->io_base + PATA_REG_LBA1, (uint8_t)(lba >> 32));
        outb(d->io_base + PATA_REG_LBA2, (uint8_t)(lba >> 40));
    }
    outb(d->io_base + PATA_REG_SECCOUNT, (uint8_t)count);
    outb(d->io_base + PATA_REG_LBA0, (uint8_t)lba);
    outb(d->io_base + PATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(d->io_base + PATA_REG_LBA2, (uint8_t)(lba >> 16));
    *ext_out = ext;
    return 0;
}

static int pata_transfer(struct pata_driver_private_data* d, uint64_t lba, int count,
                         void* buf, bool write){
    bool ext = false;
    int res = pata_setup(d, lba, count, &ext);
    if(res < 0){
        return res;
    }
    uint8_t cmd = write ? (ext ? PATA_CMD_WRITE_PIO_EXT : PATA_CMD_WRITE_PIO)
                        : (ext ? PATA_CMD_READ_PIO_EXT  : PATA_CMD_READ_PIO);
    outb(d->io_base + PATA_REG_COMMAND, cmd);

    uint16_t* ptr = buf;
    for(int s = 0; s < count; s++){
        pata_delay400(d);
        res = pata_wait_drq(d);
        if(res < 0){
            return res;
        }
        if(write){
            for(int w = 0; w < 256; w++){
                outw(d->io_base + PATA_REG_DATA, *ptr++);
            }
        } else {
            for(int w = 0; w < 256; w++){
                *ptr++ = insw(d->io_base + PATA_REG_DATA);
            }
        }
    }

    if(write){
        pata_delay400(d);
        res = pata_wait_not_busy(d);
        if(res < 0){
            return res;
        }
        outb(d->io_base + PATA_REG_COMMAND, ext ? PATA_CMD_CACHE_FLUSH_EXT : PATA_CMD_CACHE_FLUSH);
        pata_delay400(d);
        res = pata_wait_not_busy(d);
        if(res < 0){
            return res;
        }
        if(pata_status(d) & (PATA_SR_ERR | PATA_SR_DF)){
            return -EIO;
        }
    }
    return 0;
}

static int pata_io(struct disk* disk, uint64_t lba, int total, void* buf, bool write){
    struct pata_driver_private_data* d = disk_private_data_driver(disk);
    if(!d){
        return -EIO;
    }
    if(lba >= d->total_sectors || (uint64_t)total > d->total_sectors - lba){
        return -EIO;
    }
    char* p = buf;
    while(total > 0){
        int count = total > PATA_MAX_SECTORS_PER_CMD ? PATA_MAX_SECTORS_PER_CMD : total;
        int res = pata_transfer(d, lba, count, p, write);
        if(res < 0){
            return res;
        }
        lba   += count;
        p     += count * KERNEL_PATA_SECTOR_SIZE;
        total -= count;
    }
    return 0;
}

int pata_driver_read(struct disk* disk, uint64_t lba, int total_sectors, void* buf_out){
    return pata_io(disk, lba, total_sectors, buf_out, false);
}

int pata_driver_write(struct disk* disk, uint64_t lba, int total_sectors, void* buf_in){
    return pata_io(disk, lba, total_sectors, buf_in, true);
}

// Find the IDE controller and its I/O ports. Returns false when
// there is no PCI IDE controller (q35 has none; it uses AHCI).
static bool pata_find_ports(uint16_t io[2], uint16_t ctrl[2]){
    size_t total_pci = pci_device_count();
    for(size_t i = 0; i < total_pci; i++){
        struct pci_device* device = NULL;
        if(pci_device_get(i, &device) < 0 || !device){
            continue;
        }
        if(device->class.base != PATA_PCI_BASE_CLASS ||
           device->class.subclass != PATA_PCI_SUBCLASS){
            continue;
        }
        uint8_t prog_if = pci_cfg_read_byte(device->addr.bus, device->addr.slot,
                                            device->addr.func, PCI_HEADER_PROG_IF_OFFSET);
        io[0]   = PATA_PRIMARY_DRIVE_BASE_ADDRESS;
        ctrl[0] = PATA_PRIMARY_DRIVE_CTRL_ADDRESS;
        io[1]   = PATA_SECONDARY_DRIVE_BASE_ADDRESS;
        ctrl[1] = PATA_SECONDARY_DRIVE_CTRL_ADDRESS;
        // prog_if bit 0 / bit 2: channel in PCI native mode, with
        // its ports in BAR0/1 (primary) and BAR2/3 (secondary).
        if((prog_if & 0x01) && device->bars[0].type == PCI_DEVICE_IO_PORT && device->bars[0].addr){
            io[0]   = (uint16_t)device->bars[0].addr;
            ctrl[0] = (uint16_t)device->bars[1].addr + 2;
        }
        if((prog_if & 0x04) && device->bars[2].type == PCI_DEVICE_IO_PORT && device->bars[2].addr){
            io[1]   = (uint16_t)device->bars[2].addr;
            ctrl[1] = (uint16_t)device->bars[3].addr + 2;
        }
        return true;
    }
    return false;
}

int pata_driver_mount(struct disk_driver* driver){
    uint16_t io[2], ctrl[2];
    int found = 0;
    if(!pata_find_ports(io, ctrl)){
        return -ENOENT;
    }
    for(int channel = 0; channel < 2; channel++){
        for(int slave = 0; slave < 2; slave++){
            struct pata_driver_private_data* d = kzalloc(sizeof(*d));
            if(!d){
                return -ENOMEM;
            }
            d->disk_drive = channel == 0 ? PATA_PRIMARY_DRIVE : PATA_SECONDARY_DRIVE;
            d->slave      = slave;
            d->io_base    = io[channel];
            d->ctrl_base  = ctrl[channel];
            if(pata_identify(d) < 0){
                kfree(d);
                continue;
            }
            if(disk_create_new(driver, NULL, SAMOS_DISK_TYPE_REAL, 0, d->total_sectors,
                               KERNEL_PATA_SECTOR_SIZE, d, &d->real_disk) < 0){
                kfree(d);
                continue;
            }
            found++;
        }
    }
    return found ? 0 : -ENOENT;
}

void pata_driver_unmount(struct disk* disk){
    if(disk->type != SAMOS_DISK_TYPE_REAL){
        return;   // partitions share the parent's private data
    }
    struct pata_driver_private_data* private_data = disk_private_data_driver(disk);
    if(private_data){
        kfree(private_data);
    }
}

int pata_driver_loaded(struct disk_driver* driver){
    return 0;
}

void pata_driver_unloaded(struct disk_driver* driver){
    // do nothing..
}

struct disk_driver pata_driver = {
    .name = {"PATA"},
    .functions.loaded          = pata_driver_loaded,
    .functions.unloaded        = pata_driver_unloaded,
    .functions.read            = pata_driver_read,
    .functions.write           = pata_driver_write,
    .functions.mount           = pata_driver_mount,
    .functions.unmount         = pata_driver_unmount,
};

struct disk_driver* pata_driver_init(){
    return &pata_driver;
}
