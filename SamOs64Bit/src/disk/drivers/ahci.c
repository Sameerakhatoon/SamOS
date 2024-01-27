// AHCI (SATA) disk driver.
//
// Bring-up per the AHCI 1.3.1 spec, section 10:
//   1. enable bus mastering + memory decode, map ABAR (BAR5)
//      uncached, take the HBA from the firmware (BOHC) if it
//      supports handoff, set GHC.AE;
//   2. for each implemented port with a device present (SSTS.DET
//      = 3) and an ATA signature: stop the port, point CLB/FB at
//      our command list and received-FIS area, clear errors,
//      start it again;
//   3. IDENTIFY DEVICE for the size and model.
// I/O uses command slot 0 only. It waits for the
// completion interrupt (PCI INTx through the IOAPIC), then reads
// PxCI; without a route for the interrupt it polls PxCI. Data moves
// through a per-port bounce buffer with one PRDT entry per 4 KiB
// page, so neither the caller's buffer nor the bounce buffer has
// to be physically contiguous or aligned.

#include "ahci.h"
#include "status.h"
#include "disk/disk.h"
#include "io/pci.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "arch/arch.h"
#include "kernel.h"
#include "disk/poll.h"


static inline uint32_t ahci_read(struct ahci_hba* hba, uint32_t off){
    return mmio_read32(hba->abar + off);
}

static inline void ahci_write(struct ahci_hba* hba, uint32_t off, uint32_t v){
    mmio_write32(hba->abar + off, v);
}

static inline uint32_t ahci_port_read(struct ahci_port_private* p, uint32_t reg){
    return ahci_read(p->hba, AHCI_PORT_BASE(p->port) + reg);
}

static inline void ahci_port_write(struct ahci_port_private* p, uint32_t reg, uint32_t v){
    ahci_write(p->hba, AHCI_PORT_BASE(p->port) + reg, v);
}

// The device needs physical addresses. Kernel memory is
// direct-mapped, so translate; there is no identity fallback.
static uint64_t ahci_phys(void* virt){
    return paging_kernel_virt_to_phys(virt);
}

static void* ahci_alloc_aligned(size_t size, size_t align){
    uintptr_t raw = (uintptr_t)kzalloc(size + align);
    if(!raw){
        return NULL;
    }
    return (void*)((raw + align - 1) & ~(uintptr_t)(align - 1));
}

// The registers go into the direct map.
static void* ahci_map_mmio(uint64_t base, uint64_t size){
    return paging_map_mmio(base, size, PAGING_CACHE_DISABLED);
}

static int ahci_wait_clear(struct ahci_port_private* p, uint32_t reg, uint32_t bits){
    for(uint64_t end = disk_poll_deadline(); !disk_poll_expired(end); ){
        if(!(ahci_port_read(p, reg) & bits)){
            return 0;
        }
    }
    return -ETIMEOUT;
}

static int ahci_port_stop(struct ahci_port_private* p){
    uint32_t cmd = ahci_port_read(p, AHCI_PxCMD);
    ahci_port_write(p, AHCI_PxCMD, cmd & ~AHCI_PxCMD_ST);
    if(ahci_wait_clear(p, AHCI_PxCMD, AHCI_PxCMD_CR) < 0){
        return -ETIMEOUT;
    }
    cmd = ahci_port_read(p, AHCI_PxCMD);
    ahci_port_write(p, AHCI_PxCMD, cmd & ~AHCI_PxCMD_FRE);
    return ahci_wait_clear(p, AHCI_PxCMD, AHCI_PxCMD_FR);
}

static int ahci_port_start(struct ahci_port_private* p){
    if(ahci_wait_clear(p, AHCI_PxCMD, AHCI_PxCMD_CR) < 0){
        return -ETIMEOUT;
    }
    uint32_t cmd = ahci_port_read(p, AHCI_PxCMD);
    ahci_port_write(p, AHCI_PxCMD, cmd | AHCI_PxCMD_FRE | AHCI_PxCMD_SUD | AHCI_PxCMD_POD);
    if(ahci_wait_clear(p, AHCI_PxTFD, AHCI_TFD_BSY | AHCI_TFD_DRQ) < 0){
        return -ETIMEOUT;
    }
    cmd = ahci_port_read(p, AHCI_PxCMD);
    ahci_port_write(p, AHCI_PxCMD, cmd | AHCI_PxCMD_ST);
    return 0;
}

static int ahci_port_setup(struct ahci_port_private* p){
    // One 4 KiB page holds the command list (1 KiB), the received
    // FIS area (256 B) and the command table (0x80 + 8 * 16 B).
    uint8_t* page = ahci_alloc_aligned(4096, 4096);
    p->bounce     = ahci_alloc_aligned(AHCI_BOUNCE_PAGES * 4096, 4096);
    if(!page || !p->bounce){
        return -ENOMEM;
    }
    p->cmd_list = (struct ahci_cmd_header*)page;
    p->fis      = page + 1024;
    p->table    = (struct ahci_cmd_table*)(page + 2048);

    int res = ahci_port_stop(p);
    if(res < 0){
        return res;
    }
    uint64_t clb = ahci_phys(p->cmd_list);
    uint64_t fb  = ahci_phys(p->fis);
    ahci_port_write(p, AHCI_PxCLB,  (uint32_t)clb);
    ahci_port_write(p, AHCI_PxCLBU, (uint32_t)(clb >> 32));
    ahci_port_write(p, AHCI_PxFB,   (uint32_t)fb);
    ahci_port_write(p, AHCI_PxFBU,  (uint32_t)(fb >> 32));
    ahci_port_write(p, AHCI_PxSERR, 0xFFFFFFFFu);
    ahci_port_write(p, AHCI_PxIS,   0xFFFFFFFFu);
    ahci_port_write(p, AHCI_PxIE,   0);
    return ahci_port_start(p);
}

// Slot 0 has finished, or the port stopped on a task-file error.
static bool ahci_port_done(void* ctx){
    struct ahci_port_private* p = ctx;
    return !(ahci_port_read(p, AHCI_PxCI) & 1) ||
           ((ahci_port_read(p, AHCI_PxIS) | p->irq_status) & AHCI_PxIS_TFES);
}

// Issue one command in slot 0 and wait for completion. `bytes`
// of the bounce buffer take part in the transfer.
static int ahci_command(struct ahci_port_private* p, uint8_t command, uint64_t lba,
                        uint32_t count, uint32_t bytes, bool write){
    if(ahci_wait_clear(p, AHCI_PxTFD, AHCI_TFD_BSY | AHCI_TFD_DRQ) < 0){
        return -ETIMEOUT;
    }

    struct ahci_cmd_header* h = &p->cmd_list[0];
    struct ahci_cmd_table*  t = p->table;
    memset(t, 0, sizeof(*t));

    int prdtl = 0;
    for(uint32_t done = 0; done < bytes; done += 4096){
        uint32_t chunk = bytes - done > 4096 ? 4096 : bytes - done;
        uint64_t phys = ahci_phys(p->bounce + done);
        t->prdt[prdtl].dba  = (uint32_t)phys;
        t->prdt[prdtl].dbau = (uint32_t)(phys >> 32);
        t->prdt[prdtl].dbc  = chunk - 1;
        prdtl++;
    }

    uint8_t* fis = t->cfis;
    fis[0]  = AHCI_FIS_TYPE_REG_H2D;
    fis[1]  = 0x80;                 // C: this is a command
    fis[2]  = command;
    fis[4]  = (uint8_t)lba;
    fis[5]  = (uint8_t)(lba >> 8);
    fis[6]  = (uint8_t)(lba >> 16);
    fis[7]  = 0x40;                 // LBA mode
    fis[8]  = (uint8_t)(lba >> 24);
    fis[9]  = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);
    fis[12] = (uint8_t)count;
    fis[13] = (uint8_t)(count >> 8);

    uint64_t ctba = ahci_phys(t);
    memset(h, 0, sizeof(*h));
    h->flags = 5 | (write ? (1 << 6) : 0);   // CFL = 5 dwords
    h->prdtl = (uint16_t)prdtl;
    h->ctba  = (uint32_t)ctba;
    h->ctbau = (uint32_t)(ctba >> 32);

    ahci_port_write(p, AHCI_PxIS, 0xFFFFFFFFu);
    p->irq_status = 0;
    disk_irq_arm(&p->hba->irq);
    smp_wmb();      // the command table before the doorbell
    ahci_port_write(p, AHCI_PxCI, 1);

    disk_irq_wait(&p->hba->irq, ahci_port_done, p);
    for(uint64_t end = disk_poll_deadline(); !disk_poll_expired(end); ){
        if((ahci_port_read(p, AHCI_PxIS) | p->irq_status) & AHCI_PxIS_TFES){
            return -EIO;
        }
        if(!(ahci_port_read(p, AHCI_PxCI) & 1)){
            if(ahci_port_read(p, AHCI_PxTFD) & AHCI_TFD_ERR){
                return -EIO;
            }
            return 0;
        }
    }
    return -ETIMEOUT;
}

static void ahci_ident_string(const uint16_t* id, int first_word, int words, char* out){
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

static int ahci_identify(struct ahci_port_private* p){
    int res = ahci_command(p, AHCI_CMD_IDENTIFY, 0, 0, 512, false);
    if(res < 0){
        return res;
    }
    uint16_t* id = (uint16_t*)p->bounce;
    uint64_t lba28 = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    uint64_t lba48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                     ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    p->total_sectors = ((id[83] & (1 << 10)) && lba48) ? lba48 : lba28;
    ahci_ident_string(id, 27, 20, p->model);
    return p->total_sectors ? 0 : -EIO;
}

static int ahci_io(struct disk* disk, uint64_t lba, int total, void* buf, bool write){
    struct ahci_port_private* p = disk_private_data_driver(disk);
    if(!p){
        return -EIO;
    }
    if(lba >= p->total_sectors || (uint64_t)total > p->total_sectors - lba){
        return -EIO;
    }
    char* ptr = buf;
    while(total > 0){
        int count = total > AHCI_SECTORS_PER_CMD ? AHCI_SECTORS_PER_CMD : total;
        uint32_t bytes = (uint32_t)count * AHCI_SECTOR_SIZE;
        if(write){
            memcpy(p->bounce, ptr, bytes);
        }
        int res = ahci_command(p, write ? AHCI_CMD_WRITE_DMA_EXT : AHCI_CMD_READ_DMA_EXT,
                               lba, count, bytes, write);
        if(res < 0){
            return res;
        }
        if(!write){
            memcpy(ptr, p->bounce, bytes);
        }
        lba   += count;
        ptr   += bytes;
        total -= count;
    }
    if(write){
        return ahci_command(p, AHCI_CMD_FLUSH_CACHE_EXT, 0, 0, 0, false);
    }
    return 0;
}

static int ahci_driver_read(struct disk* disk, uint64_t lba, int total, void* buf){
    return ahci_io(disk, lba, total, buf, false);
}

static int ahci_driver_write(struct disk* disk, uint64_t lba, int total, void* buf){
    return ahci_io(disk, lba, total, buf, true);
}

static void ahci_bios_handoff(struct ahci_hba* hba){
    if(!(ahci_read(hba, AHCI_HBA_CAP2) & AHCI_CAP2_BOH)){
        return;
    }
    ahci_write(hba, AHCI_HBA_BOHC, ahci_read(hba, AHCI_HBA_BOHC) | AHCI_BOHC_OOS);
    for(uint64_t end = disk_poll_deadline(); !disk_poll_expired(end); ){
        if(!(ahci_read(hba, AHCI_HBA_BOHC) & AHCI_BOHC_BOS)){
            break;
        }
    }
}

// The HBA on its completion line (DISK_SLOT_AHCI). The line is a shared,
// level-triggered PCI INTx: clear PxIS then IS (only then does the HBA
// drop the line), and an HBA with nothing pending was not the source.
static struct ahci_hba* ahci_irq_hba;

static void ahci_irq_handler(struct trap_frame* frame){
    struct ahci_hba* hba = ahci_irq_hba;
    uint32_t is = ahci_read(hba, AHCI_HBA_IS);
    if(!is){
        return;
    }
    for(int port = 0; port < AHCI_MAX_PORTS; port++){
        if(!(is & (1u << port))){
            continue;
        }
        uint32_t pis = ahci_read(hba, AHCI_PORT_BASE(port) + AHCI_PxIS);
        ahci_write(hba, AHCI_PORT_BASE(port) + AHCI_PxIS, pis);
        if(hba->ports[port]){
            hba->ports[port]->irq_status |= pis;
        }
    }
    ahci_write(hba, AHCI_HBA_IS, is);
    disk_irq_signal(&hba->irq);
}

// After IDENTIFY (polled): PxIE for the completion FISes and errors,
// GHC.IE, INTx unmasked at the PCI and routed through the IOAPIC.
static void ahci_irq_init(struct ahci_hba* hba){
    uint16_t inti = 0;
    int gsi = arch_irq_routable() && !ahci_irq_hba ? pci_intx_gsi(hba->device, &inti) : -ENOENT;
    if(gsi < 0){
        serial_write("disk: ahci has no IOAPIC route, polled\n");
        return;
    }
    for(int port = 0; port < AHCI_MAX_PORTS; port++){
        if(hba->ports[port]){
            ahci_port_write(hba->ports[port], AHCI_PxIS, 0xFFFFFFFFu);
            ahci_port_write(hba->ports[port], AHCI_PxIE, AHCI_PxIE_COMPLETION);
        }
    }
    ahci_write(hba, AHCI_HBA_IS, 0xFFFFFFFFu);
    hba->irq.name   = "ahci";
    hba->irq.marker = BM_FEATURE_DISK_IRQ_AHCI;
    ahci_irq_hba = hba;
    pci_intx_enable(hba->device);
    ahci_write(hba, AHCI_HBA_GHC, ahci_read(hba, AHCI_HBA_GHC) | AHCI_GHC_IE);
    bool masked = disk_irq_masked_by_cmdline();
    int line = arch_irq_attach_gsi((uint32_t)gsi, inti, DISK_SLOT_AHCI, masked, ahci_irq_handler);
    if(line < 0){
        return;
    }
    hba->irq.enabled = true;
    disk_irq_report(&hba->irq, (uint32_t)gsi, inti, line, masked);
}

static int ahci_mount_controller(struct disk_driver* driver, struct pci_device* dev){
    struct pci_device_bar* bar = &dev->bars[5];
    if(bar->type != PCI_DEVICE_IO_MEMORY || bar->addr == 0){
        return -EIO;
    }
    uint64_t base = bar->addr & ~0xFULL;
    if(dev->bars[5].flags & PCI_DEVICE_BAR_FLAG_64BIT){
        // not possible for BAR5, but keep the intent obvious
        return -EIO;
    }

    struct ahci_hba* hba = kzalloc(sizeof(*hba));
    if(!hba){
        return -ENOMEM;
    }
    hba->device = dev;

    pci_enable_bus_master(dev);
    // The ABAR is the 0x100 bytes of HBA registers plus 0x80 per
    // port: 0x1100 at most. Do not trust bar->size: pci_size_bars
    // computes ~mask + 1 in 64 bits for a 32-bit BAR, so the top
    // half is all ones.
    hba->abar = ahci_map_mmio(base, 0x1100);
    if(!hba->abar){
        kfree(hba);
        return -ENOMEM;
    }
    ahci_bios_handoff(hba);
    ahci_write(hba, AHCI_HBA_GHC, ahci_read(hba, AHCI_HBA_GHC) | AHCI_GHC_AE);

    uint32_t implemented = ahci_read(hba, AHCI_HBA_PI);
    int found = 0;
    for(int port = 0; port < AHCI_MAX_PORTS; port++){
        if(!(implemented & (1u << port))){
            continue;
        }
        struct ahci_port_private* p = kzalloc(sizeof(*p));
        if(!p){
            return -ENOMEM;
        }
        p->hba  = hba;
        p->port = port;
        uint32_t ssts = ahci_port_read(p, AHCI_PxSSTS);
        if((ssts & 0xF) != 3 || ahci_port_read(p, AHCI_PxSIG) != AHCI_SIG_ATA){
            kfree(p);
            continue;         // empty port, or ATAPI / port multiplier
        }
        if(ahci_port_setup(p) < 0 || ahci_identify(p) < 0){
            kfree(p);
            continue;
        }
        if(disk_create_new(driver, NULL, SAMOS_DISK_TYPE_REAL, 0, p->total_sectors,
                           AHCI_SECTOR_SIZE, p, &p->real_disk) < 0){
            kfree(p);
            continue;
        }
        hba->ports[port] = p;
        found++;
    }
    if(found){
        ahci_irq_init(hba);
    }
    return found;
}

static int ahci_driver_mount(struct disk_driver* driver){
    int found = 0;
    size_t total_pci = pci_device_count();
    for(size_t i = 0; i < total_pci; i++){
        struct pci_device* dev = NULL;
        if(pci_device_get(i, &dev) < 0 || !dev){
            continue;
        }
        if(dev->class.base != AHCI_PCI_BASE_CLASS || dev->class.subclass != AHCI_PCI_SUBCLASS){
            continue;
        }
        uint8_t prog_if = pci_cfg_read_byte(dev->addr.bus, dev->addr.slot, dev->addr.func,
                                            PCI_HEADER_PROG_IF_OFFSET);
        if(prog_if != 0x01){
            continue;         // not AHCI (vendor-specific SATA mode)
        }
        int res = ahci_mount_controller(driver, dev);
        if(res > 0){
            found += res;
        }
    }
    return found ? 0 : -ENOENT;
}

static void ahci_driver_unmount(struct disk* disk){
    // Port memory stays allocated: the HBA may still DMA into the
    // received-FIS area until the port is stopped.
}

static struct disk_driver ahci_driver = {
    .name = {"AHCI"},
    .functions.loaded   = NULL,
    .functions.unloaded = NULL,
    .functions.read     = ahci_driver_read,
    .functions.write    = ahci_driver_write,
    .functions.mount    = ahci_driver_mount,
    .functions.unmount  = ahci_driver_unmount,
};

struct disk_driver* ahci_driver_init(void){
    return &ahci_driver;
}
