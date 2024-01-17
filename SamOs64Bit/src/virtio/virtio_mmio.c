// virtio-mmio version 2 transport. See virtio_mmio.h.
// Portable: every access goes through mmio_read32/mmio_write32.

#include "virtio_mmio.h"
#include "virtio_io.h"
#include "status.h"

static inline volatile uint8_t* vmmio(struct virtio_dev* d, uint32_t off){
    return (volatile uint8_t*)d->bus + off;
}

static inline uint32_t vmmio_read(struct virtio_dev* d, uint32_t off){
    return mmio_read32(vmmio(d, off));
}

static inline void vmmio_write(struct virtio_dev* d, uint32_t off, uint32_t v){
    mmio_write32(vmmio(d, off), v);
}

static uint8_t vmmio_get_status(struct virtio_dev* d){
    return (uint8_t)vmmio_read(d, VIRTIO_MMIO_STATUS);
}

static void vmmio_set_status(struct virtio_dev* d, uint8_t s){
    vmmio_write(d, VIRTIO_MMIO_STATUS, s);
}

static uint64_t vmmio_get_features(struct virtio_dev* d){
    vmmio_write(d, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    uint32_t lo = vmmio_read(d, VIRTIO_MMIO_DEVICE_FEATURES);
    vmmio_write(d, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
    uint32_t hi = vmmio_read(d, VIRTIO_MMIO_DEVICE_FEATURES);
    return ((uint64_t)hi << 32) | lo;
}

static void vmmio_set_features(struct virtio_dev* d, uint64_t f){
    vmmio_write(d, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    vmmio_write(d, VIRTIO_MMIO_DRIVER_FEATURES, (uint32_t)f);
    vmmio_write(d, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
    vmmio_write(d, VIRTIO_MMIO_DRIVER_FEATURES, (uint32_t)(f >> 32));
}

static uint16_t vmmio_queue_max(struct virtio_dev* d, uint16_t index){
    vmmio_write(d, VIRTIO_MMIO_QUEUE_SEL, index);
    if(vmmio_read(d, VIRTIO_MMIO_QUEUE_READY)){
        return 0;
    }
    uint32_t max = vmmio_read(d, VIRTIO_MMIO_QUEUE_NUM_MAX);
    return max > 0xFFFF ? 0xFFFF : (uint16_t)max;
}

static int vmmio_queue_enable(struct virtio_dev* d, struct virtq* q){
    vmmio_write(d, VIRTIO_MMIO_QUEUE_SEL, q->index);
    vmmio_write(d, VIRTIO_MMIO_QUEUE_NUM, q->size);
    vmmio_write(d, VIRTIO_MMIO_QUEUE_DESC_LOW,    (uint32_t)q->desc_phys);
    vmmio_write(d, VIRTIO_MMIO_QUEUE_DESC_HIGH,   (uint32_t)(q->desc_phys >> 32));
    vmmio_write(d, VIRTIO_MMIO_QUEUE_DRIVER_LOW,  (uint32_t)q->avail_phys);
    vmmio_write(d, VIRTIO_MMIO_QUEUE_DRIVER_HIGH, (uint32_t)(q->avail_phys >> 32));
    vmmio_write(d, VIRTIO_MMIO_QUEUE_DEVICE_LOW,  (uint32_t)q->used_phys);
    vmmio_write(d, VIRTIO_MMIO_QUEUE_DEVICE_HIGH, (uint32_t)(q->used_phys >> 32));
    vmmio_write(d, VIRTIO_MMIO_QUEUE_READY, 1);
    q->notify = vmmio(d, VIRTIO_MMIO_QUEUE_NOTIFY);
    return vmmio_read(d, VIRTIO_MMIO_QUEUE_READY) ? 0 : -EIO;
}

static void vmmio_notify(struct virtio_dev* d, struct virtq* q){
    mmio_write32(q->notify, q->index);
}

static uint8_t vmmio_isr(struct virtio_dev* d){
    uint32_t s = vmmio_read(d, VIRTIO_MMIO_INTERRUPT_STATUS);
    if(s){
        vmmio_write(d, VIRTIO_MMIO_INTERRUPT_ACK, s);
    }
    return (uint8_t)s;
}

static uint8_t vmmio_generation(struct virtio_dev* d){
    return (uint8_t)vmmio_read(d, VIRTIO_MMIO_CONFIG_GENERATION);
}

static uint8_t vmmio_cfg_read8(struct virtio_dev* d, uint32_t off){
    return mmio_read8(vmmio(d, VIRTIO_MMIO_CONFIG + off));
}

static uint32_t vmmio_cfg_read32(struct virtio_dev* d, uint32_t off){
    return vmmio_read(d, VIRTIO_MMIO_CONFIG + off);
}

static void vmmio_cfg_write8(struct virtio_dev* d, uint32_t off, uint8_t v){
    mmio_write8(vmmio(d, VIRTIO_MMIO_CONFIG + off), v);
}

static const struct virtio_ops virtio_mmio_ops = {
    .get_status        = vmmio_get_status,
    .set_status        = vmmio_set_status,
    .get_features      = vmmio_get_features,
    .set_features      = vmmio_set_features,
    .queue_max         = vmmio_queue_max,
    .queue_enable      = vmmio_queue_enable,
    .notify            = vmmio_notify,
    .isr               = vmmio_isr,
    .config_generation = vmmio_generation,
    .cfg_read8         = vmmio_cfg_read8,
    .cfg_read32        = vmmio_cfg_read32,
    .cfg_write8        = vmmio_cfg_write8,
};

int virtio_mmio_attach(volatile void* base, struct virtio_dev* d){
    d->bus = (void*)base;
    d->ops = &virtio_mmio_ops;
    d->transport = "mmio";
    d->features = 0;
    if(vmmio_read(d, VIRTIO_MMIO_MAGIC_VALUE) != VIRTIO_MMIO_MAGIC){
        return -EIO;
    }
    // Version 1 is the legacy layout (QueuePFN); only version 2 here.
    if(vmmio_read(d, VIRTIO_MMIO_VERSION) != 2){
        return -EIO;
    }
    d->device_id = vmmio_read(d, VIRTIO_MMIO_DEVICE_ID);
    return d->device_id ? 0 : -ENOENT;
}
