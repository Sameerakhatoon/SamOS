#ifndef KERNEL_DISK_DRIVERS_PATA_H
#define KERNEL_DISK_DRIVERS_PATA_H
// Lecture 187 - PATA (parallel-ATA / IDE) disk driver header.
//
// All four drives (primary/secondary channel, master/
// slave) are probed with IDENTIFY, which gives the size and
// whether LBA48 is supported. Reads and writes are polled PIO,
// split into commands of at most PATA_MAX_SECTORS_PER_CMD.

#include <stdint.h>
#include <stdbool.h>
#include "disk.h"
#include "driver.h"

#define KERNEL_PATA_SECTOR_SIZE 512
#define PATA_PCI_BASE_CLASS 0x01
#define PATA_PCI_SUBCLASS   0x01

#define PATA_INVALID_BASE_ADDRESS         -1
#define PATA_PRIMARY_DRIVE_BASE_ADDRESS   0x1F0
#define PATA_SECONDARY_DRIVE_BASE_ADDRESS 0x170
#define PATA_PRIMARY_DRIVE_CTRL_ADDRESS   0x3F6
#define PATA_SECONDARY_DRIVE_CTRL_ADDRESS 0x376

// LBA28 can address 2^28 sectors (128 GiB); anything at or past
// that needs LBA48.
#define PATA_LBA28_LIMIT          0x10000000ULL
// One command moves at most this many sectors. LBA28 allows 256
// and LBA48 65536; 128 keeps the polling loop short and avoids
// the "count byte 0 means 256" edge entirely.
#define PATA_MAX_SECTORS_PER_CMD  128

// ATA register offsets from the I/O base.
#define PATA_REG_DATA        0x00
#define PATA_REG_ERROR       0x01
#define PATA_REG_SECCOUNT    0x02
#define PATA_REG_LBA0        0x03
#define PATA_REG_LBA1        0x04
#define PATA_REG_LBA2        0x05
#define PATA_REG_DRIVE       0x06
#define PATA_REG_STATUS      0x07
#define PATA_REG_COMMAND     0x07

#define PATA_SR_ERR  0x01
#define PATA_SR_DRQ  0x08
#define PATA_SR_DF   0x20
#define PATA_SR_BSY  0x80

#define PATA_CMD_READ_PIO        0x20
#define PATA_CMD_READ_PIO_EXT    0x24
#define PATA_CMD_WRITE_PIO       0x30
#define PATA_CMD_WRITE_PIO_EXT   0x34
#define PATA_CMD_CACHE_FLUSH     0xE7
#define PATA_CMD_CACHE_FLUSH_EXT 0xEA
#define PATA_CMD_IDENTIFY        0xEC

enum {
    PATA_INVALID_DRIVE   = -1,
    PATA_PRIMARY_DRIVE   = 0,
    PATA_SECONDARY_DRIVE = 1,
    PATA_PARTITION_DRIVE = 2,
};

typedef int PATA_DISK_DRIVE;

struct disk;
struct pata_driver_private_data {
    PATA_DISK_DRIVE disk_drive;   // channel: PRIMARY or SECONDARY
    bool            slave;
    bool            lba48;
    uint16_t        io_base;
    uint16_t        ctrl_base;
    uint64_t        total_sectors;
    char            model[41];
    struct disk*    real_disk;
};

struct disk_driver* pata_driver_init();

#endif
