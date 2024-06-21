// virtio-blk disk driver (virtio 1.x, spec 5.2).
//
// One request queue (queue 0), one request in flight: the disk layer
// calls drivers under disk_lock, so the driver keeps no lock of
// its own. A request is a chain of three or more descriptors: the
// 16-byte header (type, sector), the data, one status byte the device
// writes. Data moves through a bounce buffer of VBLK_BOUNCE_PAGES
// separate pages, one descriptor each, so neither the caller's buffer
// nor the bounce buffer has to be physically contiguous (as AHCI).
//
// Features taken when offered: RO (the disk is marked read-only, the
// layer refuses writes with -ERDONLY) and FLUSH (every write is
// followed by a flush, like AHCI's FLUSH CACHE EXT). The sector size
// is 512 whatever BLK_SIZE says; capacity is in 512-byte sectors.
//
// Completion: the used ring. With a line (virtio_bus.h: PCI INTx, or
// An MMIO slot's interrupt) the wait is disk_irq_wait: the
// line's handler reads the ISR and signals; without one, or after
// three missed interrupts, it polls the used ring until
// disk_poll_deadline. A request that times out
// leaves the device owning the buffers, so the disk is marked broken
// and every later request fails instead of reading a stale completion.

#include "virtio_blk.h"
#include "virtio/virtio.h"
#include "virtio/virtio_bus.h"
#include "virtio/virtio_io.h"
#include "disk/disk.h"
#include "disk/irqwait.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "status.h"
#include "kernel.h"

#define VIRTIO_BLK_F_RO         5
#define VIRTIO_BLK_F_FLUSH      9

#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1
#define VIRTIO_BLK_T_FLUSH      4
#define VIRTIO_BLK_S_OK         0

#define VIRTIO_BLK_CFG_CAPACITY 0

#define VBLK_SECTOR_SIZE        512
#define VBLK_BOUNCE_PAGES       16         // 64 KiB, 128 sectors per request
#define VBLK_SECTORS_PER_REQ    (VBLK_BOUNCE_PAGES * 4096 / VBLK_SECTOR_SIZE)
#define VBLK_QUEUE_SIZE         64
#define VBLK_STATUS_OFFSET      64         // status byte, in the header's page
#define VBLK_MAX_DEVICES        8

struct virtio_blk_req_hdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

struct vblk {
    struct virtio_dev*  vdev;
    struct virtq        q;
    uint64_t            capacity;
    uint8_t*            req;               // header, then the status byte
    uint8_t*            bounce[VBLK_BOUNCE_PAGES];
    struct disk*        disk;
    bool                broken;
    struct virtio_line* line;
    struct disk_irq*    irq;               // own_irq, or that of the first vblk on the line
    struct disk_irq     own_irq;
};

static struct vblk* vblk_devices[VBLK_MAX_DEVICES];
static int vblk_device_count;

static bool vblk_done(void* ctx){
    return virtq_has_used(&((struct vblk*)ctx)->q);
}

// One request, bytes of the bounce buffer taking part. 0, -EIO for a
// device error status, -ETIMEOUT.
static int vblk_request(struct vblk* b, uint32_t type, uint64_t sector, uint32_t bytes){
    struct virtio_blk_req_hdr* h = (struct virtio_blk_req_hdr*)b->req;
    volatile uint8_t* status = b->req + VBLK_STATUS_OFFSET;
    h->type     = type;
    h->reserved = 0;
    h->sector   = sector;
    *status     = 0xFF;

    struct virtq_buf bufs[VBLK_BOUNCE_PAGES + 2];
    int n = 0;
    bufs[n++] = (struct virtq_buf){ virtio_dma_phys(h), sizeof(*h), false };
    for(uint32_t done = 0; done < bytes; done += 4096){
        uint32_t chunk = bytes - done > 4096 ? 4096 : bytes - done;
        bufs[n++] = (struct virtq_buf){ virtio_dma_phys(b->bounce[done / 4096]), chunk,
                                        type == VIRTIO_BLK_T_IN };
    }
    bufs[n++] = (struct virtq_buf){ virtio_dma_phys((void*)status), 1, true };
    if(virtq_add(&b->q, bufs, n) < 0){
        return -EIO;
    }
    // The header and the status byte (its own line) to memory;
    // the bounce buffer was cleaned or flushed by vblk_io.
    virtio_dma_clean(h, sizeof(*h));
    virtio_dma_flush(status, 1);

    disk_irq_arm(b->irq);
    virtq_kick(&b->q);
    disk_irq_wait(b->irq, vblk_done, b);
    for(uint64_t end = disk_poll_deadline(); !virtq_has_used(&b->q); ){
        if(disk_poll_expired(end)){
            b->broken = true;
            serial_write("virtio-blk: request timed out, disk disabled\n");
            return -ETIMEOUT;
        }
    }
    virtq_pop_used(&b->q, NULL, NULL);
    virtio_dma_inval(status, 1);
    return *status == VIRTIO_BLK_S_OK ? 0 : -EIO;
}

// Written bounce pages go to memory before the request; pages
// the device fills are flushed before it (no dirty line of ours may
// land on its data) and dropped after it, before the copy.
static void vblk_copy(struct vblk* b, uint8_t* ptr, uint32_t bytes, bool to_device){
    for(uint32_t done = 0; done < bytes; done += 4096){
        uint32_t chunk = bytes - done > 4096 ? 4096 : bytes - done;
        if(to_device){
            memcpy(b->bounce[done / 4096], ptr + done, chunk);
            virtio_dma_clean(b->bounce[done / 4096], chunk);
        } else {
            virtio_dma_inval(b->bounce[done / 4096], chunk);
            memcpy(ptr + done, b->bounce[done / 4096], chunk);
        }
    }
}

static void vblk_flush_bounce(struct vblk* b, uint32_t bytes){
    for(uint32_t done = 0; done < bytes; done += 4096){
        virtio_dma_flush(b->bounce[done / 4096], bytes - done > 4096 ? 4096 : bytes - done);
    }
}

static int vblk_io(struct disk* disk, uint64_t lba, int total, void* buf, bool write){
    struct vblk* b = disk_private_data_driver(disk);
    if(!b || b->broken || total <= 0){
        return -EIO;
    }
    if(lba >= b->capacity || (uint64_t)total > b->capacity - lba){
        return -EIO;
    }
    if(write && virtio_has(b->vdev, VIRTIO_BLK_F_RO)){
        return -ERDONLY;
    }
    uint8_t* ptr = buf;
    while(total > 0){
        int count = total > VBLK_SECTORS_PER_REQ ? VBLK_SECTORS_PER_REQ : total;
        uint32_t bytes = (uint32_t)count * VBLK_SECTOR_SIZE;
        if(write){
            vblk_copy(b, ptr, bytes, true);
        } else {
            vblk_flush_bounce(b, bytes);
        }
        int res = vblk_request(b, write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN, lba, bytes);
        if(res < 0){
            return res;
        }
        if(!write){
            vblk_copy(b, ptr, bytes, false);
        }
        lba   += count;
        ptr   += bytes;
        total -= count;
    }
    if(write && virtio_has(b->vdev, VIRTIO_BLK_F_FLUSH)){
        return vblk_request(b, VIRTIO_BLK_T_FLUSH, 0, 0);
    }
    return 0;
}

static int vblk_read(struct disk* disk, uint64_t lba, int total, void* buf){
    return vblk_io(disk, lba, total, buf, false);
}

static int vblk_write(struct disk* disk, uint64_t lba, int total, void* buf){
    return vblk_io(disk, lba, total, buf, true);
}

// The line's handler: this device's ISR showed a used-ring update.
static void vblk_irq_signal(void* ctx){
    disk_irq_signal(((struct vblk*)ctx)->irq);
}

// After DRIVER_OK. vblks on one line share one struct disk_irq, as the
// ports of an AHCI HBA do: the disk wait steers the line, so the line
// has one destination and one waiter (disk_lock) at a time.
static void vblk_irq_init(struct vblk* b){
    bool masked = disk_irq_masked_by_cmdline();
    b->line = virtio_irq(b->vdev, masked, vblk_irq_signal, b);
    if(!b->line){
        serial_write("disk: virtio-blk has no IOAPIC route, polled\n");
        return;
    }
    for(int i = 0; i < vblk_device_count; i++){
        if(vblk_devices[i] != b && vblk_devices[i]->line == b->line){
            b->irq = vblk_devices[i]->irq;
            return;
        }
    }
    b->irq->name    = "virtio-blk";
    b->irq->marker  = BM_FEATURE_DISK_IRQ_VIRTIO;
    b->irq->enabled = true;
    disk_irq_report(b->irq, b->line->gsi, b->line->inti, b->line->vector, masked);
}

static void vblk_free(struct vblk* b){
    virtio_dma_free(b->q.page);
    virtio_dma_free(b->req);
    for(int i = 0; i < VBLK_BOUNCE_PAGES; i++){
        virtio_dma_free(b->bounce[i]);
    }
    kfree(b);
}

static int vblk_attach(struct virtio_dev* d, void* ctx){
    struct disk_driver* driver = ctx;
    if(vblk_device_count == VBLK_MAX_DEVICES){
        return -ENOSPC;
    }
    struct vblk* b = kzalloc(sizeof(*b));
    if(!b){
        return -ENOMEM;
    }
    b->vdev = d;
    b->irq  = &b->own_irq;
    int res = virtio_negotiate(d, (1ULL << VIRTIO_BLK_F_RO) | (1ULL << VIRTIO_BLK_F_FLUSH));
    if(res < 0){
        kfree(b);
        return res;
    }
    b->capacity = virtio_cfg_read64(d, VIRTIO_BLK_CFG_CAPACITY);
    b->req = virtio_dma_page();
    for(int i = 0; i < VBLK_BOUNCE_PAGES; i++){
        b->bounce[i] = virtio_dma_page();
        if(!b->bounce[i]){
            res = -ENOMEM;
        }
    }
    if(!b->req || res < 0 || b->capacity == 0){
        res = res < 0 ? res : -EIO;
        goto fail;
    }
    res = virtq_setup(d, &b->q, 0, VBLK_QUEUE_SIZE);
    if(res == 0 && b->q.size < VBLK_BOUNCE_PAGES + 2){
        res = -EIO;                // a full request would never fit
    }
    if(res < 0){
        goto fail;
    }
    virtio_driver_ok(d);

    vblk_devices[vblk_device_count++] = b;
    vblk_irq_init(b);
    res = disk_create_new(driver, NULL, SAMOS_DISK_TYPE_REAL, 0, b->capacity,
                          VBLK_SECTOR_SIZE, b, &b->disk);
    if(res < 0){
        // The line keeps a client pointing here; leave b allocated and
        // the device stopped rather than free memory a handler reads.
        virtio_reset(d);
        b->broken = true;
        return res;
    }
    if(virtio_has(d, VIRTIO_BLK_F_RO)){
        b->disk->read_only = true;
    }
    serial_write("virtio-blk: disk ");
    serial_write(itoa(b->disk->id));
    serial_write(", ");
    serial_write(itoa((int)(b->capacity / 2048)));
    serial_write(" MiB");
    serial_write(virtio_has(d, VIRTIO_BLK_F_RO) ? ", read-only" : "");
    serial_write(virtio_has(d, VIRTIO_BLK_F_FLUSH) ? ", flush" : "");
    serial_write(", queue ");
    serial_write(itoa(b->q.size));
    serial_write("\n");
    return 0;

fail:
    virtio_reset(d);
    vblk_free(b);
    return res;
}

static int vblk_mount(struct disk_driver* driver){
    return virtio_probe(VIRTIO_ID_BLOCK, vblk_attach, driver) > 0 ? 0 : -ENOENT;
}

static void vblk_unmount(struct disk* disk){
    // As AHCI: the queue and buffers stay, the device may still own them.
}

static struct disk_driver virtio_blk_driver = {
    .name = {"VIRTIO-BLK"},
    .functions.loaded   = NULL,
    .functions.unloaded = NULL,
    .functions.read     = vblk_read,
    .functions.write    = vblk_write,
    .functions.mount    = vblk_mount,
    .functions.unmount  = vblk_unmount,
};

struct disk_driver* virtio_blk_driver_init(void){
    return &virtio_blk_driver;
}
