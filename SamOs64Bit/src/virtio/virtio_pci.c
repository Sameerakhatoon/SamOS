// Modern virtio-pci transport. See virtio_pci.h.

#include "virtio_pci.h"
#include "virtio_io.h"
#include "status.h"
#include "io/pci.h"
#include "io/apic.h"
#include "io/serial.h"
#include "idt/idt.h"
#include "memory/heap/kheap.h"
#include "memory/paging/paging.h"

#define PCI_STATUS_CAP_LIST         0x10
#define PCI_CAP_POINTER             0x34
#define PCI_SUBSYSTEM_ID            0x2E
#define PCI_CAP_ID_VENDOR           0x09

// struct virtio_pci_cap (spec 4.1.4)
#define VIRTIO_PCI_CAP_CFG_TYPE     3
#define VIRTIO_PCI_CAP_BAR          4
#define VIRTIO_PCI_CAP_OFFSET       8
#define VIRTIO_PCI_CAP_LENGTH       12
#define VIRTIO_PCI_CAP_NOTIFY_MULT  16

enum {
    VIRTIO_PCI_CAP_COMMON_CFG = 1,
    VIRTIO_PCI_CAP_NOTIFY_CFG = 2,
    VIRTIO_PCI_CAP_ISR_CFG    = 3,
    VIRTIO_PCI_CAP_DEVICE_CFG = 4,
};

// struct virtio_pci_common_cfg (spec 4.1.4.3)
#define VPCI_DEVICE_FEATURE_SELECT  0x00
#define VPCI_DEVICE_FEATURE         0x04
#define VPCI_DRIVER_FEATURE_SELECT  0x08
#define VPCI_DRIVER_FEATURE         0x0C
#define VPCI_DEVICE_STATUS          0x14
#define VPCI_CONFIG_GENERATION      0x15
#define VPCI_QUEUE_SELECT           0x16
#define VPCI_QUEUE_SIZE             0x18
#define VPCI_QUEUE_ENABLE           0x1C
#define VPCI_QUEUE_NOTIFY_OFF       0x1E
#define VPCI_QUEUE_DESC             0x20
#define VPCI_QUEUE_DRIVER           0x28
#define VPCI_QUEUE_DEVICE           0x30

struct virtio_pci {
    struct virtio_dev  dev;          // first: d->bus points back here
    struct pci_device* pci;
    volatile uint8_t*  common;
    volatile uint8_t*  isr;
    volatile uint8_t*  device;
    volatile uint8_t*  notify;
    uint32_t           notify_mult;
};

static inline struct virtio_pci* vpci(struct virtio_dev* d){
    return (struct virtio_pci*)d->bus;
}

static uint8_t vpci_get_status(struct virtio_dev* d){
    return mmio_read8(vpci(d)->common + VPCI_DEVICE_STATUS);
}

static void vpci_set_status(struct virtio_dev* d, uint8_t s){
    mmio_write8(vpci(d)->common + VPCI_DEVICE_STATUS, s);
}

static uint64_t vpci_get_features(struct virtio_dev* d){
    volatile uint8_t* c = vpci(d)->common;
    mmio_write32(c + VPCI_DEVICE_FEATURE_SELECT, 0);
    uint32_t lo = mmio_read32(c + VPCI_DEVICE_FEATURE);
    mmio_write32(c + VPCI_DEVICE_FEATURE_SELECT, 1);
    uint32_t hi = mmio_read32(c + VPCI_DEVICE_FEATURE);
    return ((uint64_t)hi << 32) | lo;
}

static void vpci_set_features(struct virtio_dev* d, uint64_t f){
    volatile uint8_t* c = vpci(d)->common;
    mmio_write32(c + VPCI_DRIVER_FEATURE_SELECT, 0);
    mmio_write32(c + VPCI_DRIVER_FEATURE, (uint32_t)f);
    mmio_write32(c + VPCI_DRIVER_FEATURE_SELECT, 1);
    mmio_write32(c + VPCI_DRIVER_FEATURE, (uint32_t)(f >> 32));
}

static uint16_t vpci_queue_max(struct virtio_dev* d, uint16_t index){
    volatile uint8_t* c = vpci(d)->common;
    mmio_write16(c + VPCI_QUEUE_SELECT, index);
    if(mmio_read16(c + VPCI_QUEUE_ENABLE)){
        return 0;
    }
    return mmio_read16(c + VPCI_QUEUE_SIZE);
}

// The 64-bit fields are written as two 32-bit halves, which the spec
// allows (4.1.3.1) and which every transport width supports.
static void vpci_write64(volatile uint8_t* p, uint64_t v){
    mmio_write32(p, (uint32_t)v);
    mmio_write32(p + 4, (uint32_t)(v >> 32));
}

static int vpci_queue_enable(struct virtio_dev* d, struct virtq* q){
    struct virtio_pci* p = vpci(d);
    volatile uint8_t* c = p->common;
    mmio_write16(c + VPCI_QUEUE_SELECT, q->index);
    mmio_write16(c + VPCI_QUEUE_SIZE, q->size);
    vpci_write64(c + VPCI_QUEUE_DESC,   q->desc_phys);
    vpci_write64(c + VPCI_QUEUE_DRIVER, q->avail_phys);
    vpci_write64(c + VPCI_QUEUE_DEVICE, q->used_phys);
    q->notify = p->notify + (uint32_t)mmio_read16(c + VPCI_QUEUE_NOTIFY_OFF) * p->notify_mult;
    mmio_write16(c + VPCI_QUEUE_ENABLE, 1);
    return mmio_read16(c + VPCI_QUEUE_ENABLE) ? 0 : -EIO;
}

static void vpci_notify(struct virtio_dev* d, struct virtq* q){
    mmio_write16(q->notify, q->index);
}

// Reading the ISR byte acknowledges it and deasserts INTx.
static uint8_t vpci_isr(struct virtio_dev* d){
    return mmio_read8(vpci(d)->isr);
}

static uint8_t vpci_generation(struct virtio_dev* d){
    return mmio_read8(vpci(d)->common + VPCI_CONFIG_GENERATION);
}

static uint8_t vpci_cfg_read8(struct virtio_dev* d, uint32_t off){
    return mmio_read8(vpci(d)->device + off);
}

static uint32_t vpci_cfg_read32(struct virtio_dev* d, uint32_t off){
    return mmio_read32(vpci(d)->device + off);
}

static void vpci_cfg_write8(struct virtio_dev* d, uint32_t off, uint8_t v){
    mmio_write8(vpci(d)->device + off, v);
}

static const struct virtio_ops virtio_pci_ops = {
    .get_status        = vpci_get_status,
    .set_status        = vpci_set_status,
    .get_features      = vpci_get_features,
    .set_features      = vpci_set_features,
    .queue_max         = vpci_queue_max,
    .queue_enable      = vpci_queue_enable,
    .notify            = vpci_notify,
    .isr               = vpci_isr,
    .config_generation = vpci_generation,
    .cfg_read8         = vpci_cfg_read8,
    .cfg_read32        = vpci_cfg_read32,
    .cfg_write8        = vpci_cfg_write8,
};

// The virtio type of a function, 0 if it is not virtio.
static uint32_t vpci_type(struct pci_device* dev){
    if(dev->vendor != VIRTIO_PCI_VENDOR){
        return 0;
    }
    if(dev->device_id >= 0x1040 && dev->device_id <= 0x107F){
        return dev->device_id - 0x1040u;
    }
    if(dev->device_id >= 0x1000 && dev->device_id <= 0x103F){
        return pci_cfg_read_word(dev->addr.bus, dev->addr.slot, dev->addr.func, PCI_SUBSYSTEM_ID);
    }
    return 0;
}

// Map one capability's window: BAR base plus offset, uncached. The
// length comes from the capability, not pci_size_bars.
static volatile uint8_t* vpci_map(struct pci_device* dev, uint8_t bar, uint32_t off, uint32_t len){
    if(bar > 5 || dev->bars[bar].type != PCI_DEVICE_IO_MEMORY || len == 0){
        return NULL;
    }
    uint64_t base = dev->bars[bar].addr & ~0xFULL;
    if(!base){
        return NULL;
    }
    return paging_map_mmio(base + off, len, PAGING_CACHE_DISABLED);
}

static int vpci_find_caps(struct virtio_pci* p){
    struct pci_device* dev = p->pci;
    uint8_t b = dev->addr.bus, s = dev->addr.slot, f = dev->addr.func;
    if(!(pci_cfg_read_word(b, s, f, PCI_HEADER_STATUS_OFFSET) & PCI_STATUS_CAP_LIST)){
        return -ENOENT;
    }
    uint8_t ptr = pci_cfg_read_byte(b, s, f, PCI_CAP_POINTER) & 0xFC;
    for(int guard = 0; ptr && guard < 48; guard++){
        if(pci_cfg_read_byte(b, s, f, ptr) == PCI_CAP_ID_VENDOR){
            uint8_t  type = pci_cfg_read_byte(b, s, f, ptr + VIRTIO_PCI_CAP_CFG_TYPE);
            uint8_t  bar  = pci_cfg_read_byte(b, s, f, ptr + VIRTIO_PCI_CAP_BAR);
            uint32_t off  = pci_cfg_read_dword(b, s, f, ptr + VIRTIO_PCI_CAP_OFFSET);
            uint32_t len  = pci_cfg_read_dword(b, s, f, ptr + VIRTIO_PCI_CAP_LENGTH);
            // The first capability of each type is the preferred one.
            if(type == VIRTIO_PCI_CAP_COMMON_CFG && !p->common){
                p->common = vpci_map(dev, bar, off, len);
            } else if(type == VIRTIO_PCI_CAP_NOTIFY_CFG && !p->notify){
                p->notify = vpci_map(dev, bar, off, len);
                p->notify_mult = pci_cfg_read_dword(b, s, f, ptr + VIRTIO_PCI_CAP_NOTIFY_MULT);
            } else if(type == VIRTIO_PCI_CAP_ISR_CFG && !p->isr){
                p->isr = vpci_map(dev, bar, off, len);
            } else if(type == VIRTIO_PCI_CAP_DEVICE_CFG && !p->device){
                p->device = vpci_map(dev, bar, off, len);
            }
        }
        ptr = pci_cfg_read_byte(b, s, f, ptr + 1) & 0xFC;
    }
    // A device without config space (none of ours) would leave
    // `device` NULL; the rest are mandatory.
    return p->common && p->notify && p->isr ? 0 : -ENOENT;
}

int virtio_pci_probe(uint32_t device_id, int (*attach)(struct virtio_dev* d, void* ctx), void* ctx){
    int found = 0;
    size_t total = pci_device_count();
    for(size_t i = 0; i < total; i++){
        struct pci_device* dev = NULL;
        if(pci_device_get(i, &dev) < 0 || !dev || vpci_type(dev) != device_id){
            continue;
        }
        struct virtio_pci* p = kzalloc(sizeof(*p));
        if(!p){
            break;
        }
        p->pci = dev;
        p->dev.ops = &virtio_pci_ops;
        p->dev.transport = "pci";
        p->dev.device_id = device_id;
        p->dev.bus = p;
        if(vpci_find_caps(p) < 0){
            serial_write("virtio: pci function without modern capabilities, skipped\n");
            kfree(p);
            continue;
        }
        pci_enable_bus_master(dev);
        if(attach(&p->dev, ctx) < 0){
            virtio_reset(&p->dev);
            kfree(p);
            continue;
        }
        found++;
    }
    return found;
}

// ---- INTx lines ------------------------------------------------------
// x86 only (IOAPIC, IDT vectors); the aarch64 device work/the riscv64 device work route MMIO slots through the
// GIC or PLIC instead. Built once, on the BSP, before the APs start;
// the handler only reads.

#define VIRTIO_VECTOR_BASE   (APIC_VECTOR_DISK_BASE + 4)   // irqwait.h: +0 AHCI, +2/+3 PATA
#define VIRTIO_LINES         (APIC_VECTOR_DISK_END - VIRTIO_VECTOR_BASE)

static struct virtio_line virtio_lines[VIRTIO_LINES];
static int virtio_line_count;

static void virtio_line_handler(struct trap_frame* frame){
    for(int i = 0; i < virtio_line_count; i++){
        struct virtio_line* l = &virtio_lines[i];
        if(l->vector != frame->vector){
            continue;
        }
        for(int c = 0; c < l->clients; c++){
            if(l->client[c].dev->ops->isr(l->client[c].dev) & VIRTIO_ISR_QUEUE){
                l->client[c].fn(l->client[c].ctx);
            }
        }
    }
}

struct virtio_line* virtio_pci_intx(struct virtio_dev* d, bool masked, void (*fn)(void* ctx), void* ctx){
    struct virtio_pci* p = vpci(d);
    uint16_t inti = 0;
    int gsi = apic_active() ? pci_intx_gsi(p->pci, &inti) : -ENOENT;
    if(gsi < 0){
        return NULL;
    }
    struct virtio_line* l = NULL;
    for(int i = 0; i < virtio_line_count; i++){
        if(virtio_lines[i].gsi == (uint32_t)gsi){
            l = &virtio_lines[i];
        }
    }
    bool route = !l;
    if(!l){
        if(virtio_line_count == VIRTIO_LINES){
            return NULL;
        }
        l = &virtio_lines[virtio_line_count];
        l->gsi    = (uint32_t)gsi;
        l->inti   = inti;
        l->vector = (uint8_t)(VIRTIO_VECTOR_BASE + virtio_line_count);
        l->masked = masked;
        virtio_line_count++;
        idt_register_interrupt_callback(l->vector, virtio_line_handler);
    }
    if(l->clients == VIRTIO_LINE_CLIENTS){
        return NULL;
    }
    l->client[l->clients].dev = d;
    l->client[l->clients].fn  = fn;
    l->client[l->clients].ctx = ctx;
    l->clients++;
    pci_intx_enable(p->pci);
    if(route && ioapic_route_gsi(l->gsi, l->vector, l->inti, l->masked) < 0){
        l->clients = 0;
        virtio_line_count--;       // it was the last one added
        return NULL;
    }
    return l;
}
