#ifndef KERNEL_DISK_DRIVERS_AHCI_H
#define KERNEL_DISK_DRIVERS_AHCI_H
// AHCI (SATA) disk driver. q35 machines and real PCs have
// no IDE controller; their disks sit behind an AHCI HBA (PCI
// class 01, subclass 06, prog-if 01). Polled, one command slot,
// DMA through a per-port bounce buffer.

#include <stdint.h>
#include <stdbool.h>
#include "disk.h"
#include "driver.h"
#include "disk/irqwait.h"

#define AHCI_PCI_BASE_CLASS 0x01
#define AHCI_PCI_SUBCLASS   0x06

#define AHCI_SECTOR_SIZE    512
#define AHCI_MAX_PORTS      32

// HBA (generic host control) registers.
#define AHCI_HBA_CAP    0x00
#define AHCI_HBA_GHC    0x04
#define AHCI_HBA_IS     0x08
#define AHCI_HBA_PI     0x0C
#define AHCI_HBA_VS     0x10
#define AHCI_HBA_CAP2   0x24
#define AHCI_HBA_BOHC   0x28

#define AHCI_GHC_AE     (1u << 31)
#define AHCI_GHC_IE     (1u << 1)
#define AHCI_CAP2_BOH   (1u << 0)
#define AHCI_BOHC_BOS   (1u << 0)
#define AHCI_BOHC_OOS   (1u << 1)

// Per-port registers, at 0x100 + port * 0x80.
#define AHCI_PORT_BASE(p)  (0x100u + (uint32_t)(p) * 0x80u)
#define AHCI_PxCLB      0x00
#define AHCI_PxCLBU     0x04
#define AHCI_PxFB       0x08
#define AHCI_PxFBU      0x0C
#define AHCI_PxIS       0x10
#define AHCI_PxIE       0x14
#define AHCI_PxCMD      0x18
#define AHCI_PxTFD      0x20
#define AHCI_PxSIG      0x24
#define AHCI_PxSSTS     0x28
#define AHCI_PxSERR     0x30
#define AHCI_PxCI       0x38

#define AHCI_PxCMD_ST   (1u << 0)
#define AHCI_PxCMD_SUD  (1u << 1)
#define AHCI_PxCMD_POD  (1u << 2)
#define AHCI_PxCMD_FRE  (1u << 4)
#define AHCI_PxCMD_FR   (1u << 14)
#define AHCI_PxCMD_CR   (1u << 15)

#define AHCI_PxIS_TFES  (1u << 30)
#define AHCI_PxIS_DHRS  (1u << 0)      // D2H register FIS: DMA commands, FLUSH
#define AHCI_PxIS_PSS   (1u << 1)      // PIO setup FIS: IDENTIFY
#define AHCI_PxIE_COMPLETION (AHCI_PxIS_DHRS | AHCI_PxIS_PSS | AHCI_PxIS_TFES)

#define AHCI_TFD_ERR    0x01
#define AHCI_TFD_DRQ    0x08
#define AHCI_TFD_BSY    0x80

#define AHCI_SIG_ATA    0x00000101u

#define AHCI_FIS_TYPE_REG_H2D 0x27

#define AHCI_CMD_READ_DMA_EXT    0x25
#define AHCI_CMD_WRITE_DMA_EXT   0x35
#define AHCI_CMD_FLUSH_CACHE_EXT 0xEA
#define AHCI_CMD_IDENTIFY        0xEC

// Bounce buffer: one PRDT entry per 4 KiB page, so it does not
// need to be physically contiguous.
#define AHCI_BOUNCE_PAGES        8
#define AHCI_SECTORS_PER_CMD     (AHCI_BOUNCE_PAGES * 4096 / AHCI_SECTOR_SIZE)

struct ahci_cmd_header {
    uint16_t flags;        // CFL[4:0], A, W, P, R, B, C, PMP
    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed));

struct ahci_prdt_entry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc;          // byte count - 1, bit 31 = interrupt
} __attribute__((packed));

struct ahci_cmd_table {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  reserved[48];
    struct ahci_prdt_entry prdt[AHCI_BOUNCE_PAGES];
} __attribute__((packed));

struct pci_device;
struct ahci_port_private;

struct ahci_hba {
    struct pci_device* device;
    volatile uint8_t*  abar;
    // Completion interrupt (one HBA; another one polls).
    struct disk_irq    irq;
    struct ahci_port_private* ports[AHCI_MAX_PORTS];
};

struct ahci_port_private {
    struct ahci_hba*        hba;
    int                     port;
    uint64_t                total_sectors;
    char                    model[41];
    struct ahci_cmd_header* cmd_list;   // 1 KiB aligned
    uint8_t*                fis;        // 256 B aligned
    struct ahci_cmd_table*  table;      // 128 B aligned
    uint8_t*                bounce;     // AHCI_BOUNCE_PAGES pages
    struct disk*            real_disk;
    volatile uint32_t       irq_status; // PxIS bits the handler cleared
};

struct disk_driver* ahci_driver_init(void);

#endif
