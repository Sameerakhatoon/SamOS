#ifndef VIRTIO_H
#define VIRTIO_H
// virtio 1.x core: device bring-up (status and feature
// negotiation) and split virtqueues, independent of the transport.
//
// Legacy-free: VERSION_1 is always negotiated and a device that does
// not offer it is refused, so every field is little-endian and the
// rings need no legacy layout. Transports (virtio_pci.c for the
// modern PCI capabilities, virtio_mmio.c for virtio-mmio version 2)
// fill a struct virtio_ops; drivers (virtio-blk, virtio-input) only
// talk to struct virtio_dev and struct virtq.
//
// A queue lives in one 4 KiB page: the descriptor table at 0, the
// available ring right after it, the used ring at the next 4-byte
// boundary. VIRTQ_MAX_SIZE entries is 2048 + 262 + 1030 bytes, so it
// fits, and the device sees one physically contiguous region.
//
// Not used: indirect descriptors, EVENT_IDX, packed rings. The core
// takes no lock; each driver serialises its own queue (virtio-blk
// under disk_lock, virtio-input in its interrupt handler).

#include <stdint.h>
#include <stdbool.h>

#define VIRTIO_STATUS_ACKNOWLEDGE  0x01
#define VIRTIO_STATUS_DRIVER       0x02
#define VIRTIO_STATUS_DRIVER_OK    0x04
#define VIRTIO_STATUS_FEATURES_OK  0x08
#define VIRTIO_STATUS_NEEDS_RESET  0x40
#define VIRTIO_STATUS_FAILED       0x80

#define VIRTIO_F_VERSION_1         32

#define VIRTIO_ID_NET              1
#define VIRTIO_ID_BLOCK            2
#define VIRTIO_ID_INPUT            18

// ISR status (PCI ISR capability, MMIO InterruptStatus).
#define VIRTIO_ISR_QUEUE           0x01
#define VIRTIO_ISR_CONFIG          0x02

#define VIRTQ_DESC_F_NEXT          1
#define VIRTQ_DESC_F_WRITE         2
#define VIRTQ_USED_F_NO_NOTIFY     1

#define VIRTQ_MAX_SIZE             128

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
} __attribute__((packed));

struct virtio_dev;

struct virtq {
    struct virtio_dev*  dev;
    uint16_t            index;
    uint16_t            size;
    void*               page;
    struct virtq_desc*  desc;
    struct virtq_avail* avail;
    volatile struct virtq_used* used;
    uint64_t            desc_phys, avail_phys, used_phys;
    uint16_t            free_head;
    uint16_t            num_free;
    uint16_t            last_used;      // next used->ring slot to look at
    volatile void*      notify;         // transport: where the doorbell goes
};

// One buffer of a chain: a physical address, its length, and whether
// the device writes it (device-readable otherwise).
struct virtq_buf {
    uint64_t phys;
    uint32_t len;
    bool     device_writes;
};

struct virtio_ops {
    uint8_t  (*get_status)(struct virtio_dev* d);
    void     (*set_status)(struct virtio_dev* d, uint8_t status);
    uint64_t (*get_features)(struct virtio_dev* d);
    void     (*set_features)(struct virtio_dev* d, uint64_t features);
    // Maximum size of queue `index`, 0 when there is no such queue or
    // it is already in use.
    uint16_t (*queue_max)(struct virtio_dev* d, uint16_t index);
    // Give the device q's size and ring addresses and enable it.
    int      (*queue_enable)(struct virtio_dev* d, struct virtq* q);
    void     (*notify)(struct virtio_dev* d, struct virtq* q);
    // Read and acknowledge the interrupt status (VIRTIO_ISR_*).
    uint8_t  (*isr)(struct virtio_dev* d);
    uint8_t  (*config_generation)(struct virtio_dev* d);
    uint8_t  (*cfg_read8)(struct virtio_dev* d, uint32_t off);
    uint32_t (*cfg_read32)(struct virtio_dev* d, uint32_t off);
    void     (*cfg_write8)(struct virtio_dev* d, uint32_t off, uint8_t v);
};

struct virtio_dev {
    const struct virtio_ops* ops;
    const char*              transport;    // "pci" or "mmio"
    uint32_t                 device_id;    // VIRTIO_ID_*
    uint64_t                 features;     // negotiated
    void*                    bus;          // transport private (pci: its struct virtio_pci*, mmio: the register base)
    bool                     msix;         // Every queue on MSI-X vector 0 (virtio_msix)
};

// Reset, ACKNOWLEDGE, DRIVER, then accept `wanted` & offered plus
// VERSION_1, FEATURES_OK and check the device kept it. 0, or a negative
// status with FAILED set. The queues are set up next, then
// virtio_driver_ok.
int      virtio_negotiate(struct virtio_dev* d, uint64_t wanted);
void     virtio_driver_ok(struct virtio_dev* d);
void     virtio_fail(struct virtio_dev* d);
// Stop the device: after this it no longer touches the rings.
void     virtio_reset(struct virtio_dev* d);
static inline bool virtio_has(struct virtio_dev* d, int bit){
    return (d->features >> bit) & 1;
}

// 64-bit config field, read twice around the generation counter.
uint64_t virtio_cfg_read64(struct virtio_dev* d, uint32_t off);

// Allocate queue `index` with min(device maximum, max_size) entries
// and enable it. 0 or a negative status.
int      virtq_setup(struct virtio_dev* d, struct virtq* q, uint16_t index, uint16_t max_size);
// Publish one chain; returns its head descriptor or -ENOSPC. The
// device is told only by virtq_kick.
int      virtq_add(struct virtq* q, const struct virtq_buf* bufs, int count);
void     virtq_kick(struct virtq* q);
// Something in the used ring the driver has not taken yet.
bool     virtq_has_used(struct virtq* q);
// Take one used chain, free its descriptors. False when none.
bool     virtq_pop_used(struct virtq* q, uint32_t* head_out, uint32_t* len_out);

#endif
