// virtio 1.x core: status and feature negotiation, split
// virtqueues. See virtio.h. Portable: no port I/O, no x86 header; the
// platform comes in through virtio_io.h.

#include <stddef.h>
#include "virtio.h"
#include "virtio_io.h"
#include "status.h"

#define VIRTIO_DMA_ALIGN 64u

// A reset "may take some time" (spec 4.1.4.3.2): the status reads 0
// once the device is done. Bounded by a count, not the TSC, so the
// core needs no timer.
#define VIRTIO_RESET_POLLS 1000000

void virtio_reset(struct virtio_dev* d){
    d->ops->set_status(d, 0);
    for(int i = 0; i < VIRTIO_RESET_POLLS && d->ops->get_status(d) != 0; i++){
    }
}

static void virtio_add_status(struct virtio_dev* d, uint8_t bits){
    d->ops->set_status(d, d->ops->get_status(d) | bits);
}

void virtio_fail(struct virtio_dev* d){
    virtio_add_status(d, VIRTIO_STATUS_FAILED);
}

// Spec 3.1.1, steps 1 to 6.
int virtio_negotiate(struct virtio_dev* d, uint64_t wanted){
    virtio_reset(d);
    virtio_add_status(d, VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_add_status(d, VIRTIO_STATUS_DRIVER);
    uint64_t offered = d->ops->get_features(d);
    if(!(offered & (1ULL << VIRTIO_F_VERSION_1))){
        virtio_fail(d);            // a legacy-only device
        return -EIO;
    }
    d->features = (offered & wanted) | (1ULL << VIRTIO_F_VERSION_1);
    d->ops->set_features(d, d->features);
    virtio_add_status(d, VIRTIO_STATUS_FEATURES_OK);
    if(!(d->ops->get_status(d) & VIRTIO_STATUS_FEATURES_OK)){
        virtio_fail(d);
        return -EIO;
    }
    return 0;
}

void virtio_driver_ok(struct virtio_dev* d){
    virtio_add_status(d, VIRTIO_STATUS_DRIVER_OK);
}

uint64_t virtio_cfg_read64(struct virtio_dev* d, uint32_t off){
    uint8_t gen;
    uint32_t lo, hi;
    do {
        gen = d->ops->config_generation(d);
        lo  = d->ops->cfg_read32(d, off);
        hi  = d->ops->cfg_read32(d, off + 4);
    } while(gen != d->ops->config_generation(d));
    return ((uint64_t)hi << 32) | lo;
}

int virtq_setup(struct virtio_dev* d, struct virtq* q, uint16_t index, uint16_t max_size){
    uint16_t max = d->ops->queue_max(d, index);
    if(max == 0){
        return -ENOENT;
    }
    if(max_size > VIRTQ_MAX_SIZE){
        max_size = VIRTQ_MAX_SIZE;
    }
    if(max > max_size){
        max = max_size;
    }
    // Split rings want a power of two.
    uint16_t size = 1;
    while((uint16_t)(size << 1) <= max){
        size <<= 1;
    }

    uint8_t* page = virtio_dma_page();
    if(!page){
        return -ENOMEM;
    }
    // The used ring on a cache line of its own (the spec asks for
    // 4 bytes), so dropping its lines (virtq_has_used) never drops a
    // ring write of the driver's.
    uint32_t avail_off = 16u * size;
    uint32_t used_off  = (avail_off + 6u + 2u * size + VIRTIO_DMA_ALIGN - 1) & ~(VIRTIO_DMA_ALIGN - 1);

    q->dev       = d;
    q->index     = index;
    q->size      = size;
    q->page      = page;
    q->desc      = (struct virtq_desc*)page;
    q->avail     = (struct virtq_avail*)(page + avail_off);
    q->used      = (volatile struct virtq_used*)(page + used_off);
    q->desc_phys = virtio_dma_phys(page);
    q->avail_phys = q->desc_phys + avail_off;
    q->used_phys  = q->desc_phys + used_off;
    q->last_used = 0;
    q->num_free  = size;
    q->free_head = 0;
    for(uint16_t i = 0; i < size; i++){
        q->desc[i].next = (uint16_t)(i + 1);
    }
    int res = d->ops->queue_enable(d, q);
    if(res < 0){
        virtio_dma_free(page);
        q->page = NULL;
    }
    return res;
}

int virtq_add(struct virtq* q, const struct virtq_buf* bufs, int count){
    if(count <= 0 || count > q->num_free){
        return -ENOSPC;
    }
    uint16_t head = q->free_head, i = head, last = head;
    for(int k = 0; k < count; k++){
        volatile struct virtq_desc* desc = &q->desc[i];
        desc->addr  = bufs[k].phys;
        desc->len   = bufs[k].len;
        desc->flags = (bufs[k].device_writes ? VIRTQ_DESC_F_WRITE : 0) |
                      (k + 1 < count ? VIRTQ_DESC_F_NEXT : 0);
        last = i;
        i = desc->next;
    }
    // The last descriptor's next still links the free list; without
    // F_NEXT the device ignores it.
    q->free_head = q->desc[last].next;
    q->num_free -= count;

    volatile struct virtq_avail* avail = q->avail;
    avail->ring[avail->idx % q->size] = head;
    virtio_mb();                       // the entry before the index
    avail->idx = (uint16_t)(avail->idx + 1);
    return head;
}

// The descriptors and the available ring reach memory before the
// device is told (a no-op where it snoops the caches).
void virtq_kick(struct virtq* q){
    virtio_dma_clean(q->page, q->used_phys - q->desc_phys);
    virtio_mb();                       // the index before the flag check and the doorbell
    virtio_dma_inval(q->used, 4);
    if(!(q->used->flags & VIRTQ_USED_F_NO_NOTIFY)){
        q->dev->ops->notify(q->dev, q);
    }
}

// The used ring is the device's; its lines are dropped before
// each look (virtio_dma_page wrote the zeroes back).
bool virtq_has_used(struct virtq* q){
    virtio_dma_inval(q->used, 4u + 8u * q->size);
    return q->used->idx != q->last_used;
}

bool virtq_pop_used(struct virtq* q, uint32_t* head_out, uint32_t* len_out){
    if(!virtq_has_used(q)){
        return false;
    }
    virtio_mb();                       // the index before the entry
    volatile struct virtq_used_elem* e = &q->used->ring[q->last_used % q->size];
    uint32_t head = e->id, len = e->len;
    q->last_used++;

    uint16_t i = (uint16_t)head, n = 1;
    while(q->desc[i].flags & VIRTQ_DESC_F_NEXT){
        i = q->desc[i].next;
        n++;
    }
    q->desc[i].next = q->free_head;
    q->free_head = (uint16_t)head;
    q->num_free += n;
    if(head_out){
        *head_out = head;
    }
    if(len_out){
        *len_out = len;
    }
    return true;
}
