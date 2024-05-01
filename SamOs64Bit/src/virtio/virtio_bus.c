// virtio device discovery and interrupt lines over both
// transports. See virtio_bus.h. The lines were virtio_pci.c's.

#include "virtio_bus.h"
#include "virtio_pci.h"
#include "virtio_mmio.h"
#include "virtio_io.h"
#include "status.h"
#include "memory/heap/kheap.h"

// ---- MMIO slots ------------------------------------------------------

struct virtio_mmio_slot {
    uint64_t           phys, size;
    uint32_t           irq;
    uint16_t           inti;
    struct virtio_dev* dev;          // claimed by a driver
};

static struct virtio_mmio_slot mmio_slots[VIRTIO_MMIO_MAX_SLOTS];
static int mmio_slot_count;

int virtio_mmio_add_slot(uint64_t phys, uint64_t size, uint32_t irq, uint16_t inti){
    if(mmio_slot_count == VIRTIO_MMIO_MAX_SLOTS){
        return -ENOSPC;
    }
    mmio_slots[mmio_slot_count++] = (struct virtio_mmio_slot){ phys, size, irq, inti, NULL };
    return 0;
}

static struct virtio_mmio_slot* mmio_slot_of(struct virtio_dev* d){
    for(int i = 0; i < mmio_slot_count; i++){
        if(mmio_slots[i].dev == d){
            return &mmio_slots[i];
        }
    }
    return NULL;
}

static int virtio_mmio_probe(uint32_t device_id, int (*attach)(struct virtio_dev* d, void* ctx), void* ctx){
    int found = 0;
    for(int i = 0; i < mmio_slot_count; i++){
        struct virtio_mmio_slot* s = &mmio_slots[i];
        if(s->dev){
            continue;
        }
        uint64_t size = s->size < VIRTIO_MMIO_SLOT_SIZE ? VIRTIO_MMIO_SLOT_SIZE : s->size;
        volatile void* regs = paging_map_mmio(s->phys, size, PAGING_CACHE_DISABLED);
        struct virtio_dev* d = kzalloc(sizeof(*d));
        if(!regs || !d){
            kfree(d);
            break;
        }
        if(virtio_mmio_attach(regs, d) < 0 || d->device_id != device_id){
            kfree(d);           // an empty slot, or another driver's device
            continue;
        }
        s->dev = d;             // virtio_irq finds the slot by it
        if(attach(d, ctx) < 0){
            virtio_reset(d);
            s->dev = NULL;
            kfree(d);
            continue;
        }
        found++;
    }
    return found;
}

int virtio_probe(uint32_t device_id, int (*attach)(struct virtio_dev* d, void* ctx), void* ctx){
    return virtio_pci_probe(device_id, attach, ctx) + virtio_mmio_probe(device_id, attach, ctx);
}

// ---- Lines -------------------------------------------------------------
// Built once, on the BSP, before the APs start; the handlers only read.
// A handler gets no line number from the frame, so each line has its
// own.

#define VIRTIO_SLOT_BASE     4     // disk/irqwait.h: 0 AHCI, 2-3 PATA
#define VIRTIO_LINES         8     // q35 PIRQ A-H (GSI 16-23); an MMIO slot is a line of its own

static struct virtio_line virtio_lines[VIRTIO_LINES];
static int virtio_line_count;

static void virtio_line_run(struct virtio_line* l){
    for(int c = 0; c < l->clients; c++){
        if(l->client[c].dev->ops->isr(l->client[c].dev) & VIRTIO_ISR_QUEUE){
            l->client[c].fn(l->client[c].ctx);
        }
    }
}

#define VIRTIO_LINE_HANDLER(n) \
    static void virtio_line##n(struct trap_frame* f){ virtio_line_run(&virtio_lines[n]); }
VIRTIO_LINE_HANDLER(0) VIRTIO_LINE_HANDLER(1) VIRTIO_LINE_HANDLER(2) VIRTIO_LINE_HANDLER(3)
VIRTIO_LINE_HANDLER(4) VIRTIO_LINE_HANDLER(5) VIRTIO_LINE_HANDLER(6) VIRTIO_LINE_HANDLER(7)

static INTERRUPT_CALLBACK_FUNCTION const virtio_line_handler[VIRTIO_LINES] = {
    virtio_line0, virtio_line1, virtio_line2, virtio_line3,
    virtio_line4, virtio_line5, virtio_line6, virtio_line7,
};

struct virtio_line* virtio_line_attach(struct virtio_dev* d, uint32_t gsi, uint16_t inti, bool masked,
                                       void (*fn)(void* ctx), void* ctx){
    struct virtio_line* l = NULL;
    for(int i = 0; i < virtio_line_count; i++){
        if(virtio_lines[i].gsi == gsi){
            l = &virtio_lines[i];
        }
    }
    bool route = !l;
    if(!l){
        if(virtio_line_count == VIRTIO_LINES){
            return NULL;
        }
        l = &virtio_lines[virtio_line_count];
        l->gsi    = gsi;
        l->inti   = inti;
        l->masked = masked;
    }
    if(l->clients == VIRTIO_LINE_CLIENTS){
        return NULL;
    }
    l->client[l->clients].dev = d;
    l->client[l->clients].fn  = fn;
    l->client[l->clients].ctx = ctx;
    l->clients++;
    if(route){
        int n = virtio_line_count;
        virtio_line_count++;
        int line = arch_irq_attach_gsi(l->gsi, l->inti, VIRTIO_SLOT_BASE + n, l->masked,
                                       virtio_line_handler[n]);
        if(line < 0){
            l->clients = 0;
            virtio_line_count--;
            return NULL;
        }
        l->vector = (uint8_t)line;
    }
    return l;
}

struct virtio_line* virtio_irq(struct virtio_dev* d, bool masked, void (*fn)(void* ctx), void* ctx){
    struct virtio_mmio_slot* s = mmio_slot_of(d);
    if(!s){
        return virtio_pci_intx(d, masked, fn, ctx);
    }
    return arch_irq_routable() ? virtio_line_attach(d, s->irq, s->inti, masked, fn, ctx) : NULL;
}
