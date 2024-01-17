#ifndef VIRTIO_MMIO_H
#define VIRTIO_MMIO_H
// virtio-mmio transport, version 2 (virtio 1.x, spec 4.2.2).
//
// QEMU `virt` (arm64, riscv64) has 32 of these slots at fixed
// addresses, each 0x200 bytes with one interrupt line, listed in the
// device tree as compatible = "virtio,mmio". x86 q35/pc have none, so
// on x86 this is only built and checked against a fake device by
// tests64/host/virtio_test.c. Map each slot uncached, call
// virtio_mmio_attach, skip it when it returns -ENOENT (an empty slot
// reads DeviceID 0), and hand the struct virtio_dev to the driver.

#include <stdint.h>
#include "virtio.h"

#define VIRTIO_MMIO_MAGIC_VALUE       0x000   // "virt"
#define VIRTIO_MMIO_VERSION           0x004
#define VIRTIO_MMIO_DEVICE_ID         0x008
#define VIRTIO_MMIO_VENDOR_ID         0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES   0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES   0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL         0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX     0x034
#define VIRTIO_MMIO_QUEUE_NUM         0x038
#define VIRTIO_MMIO_QUEUE_READY       0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY      0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS  0x060
#define VIRTIO_MMIO_INTERRUPT_ACK     0x064
#define VIRTIO_MMIO_STATUS            0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW    0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH   0x084
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW  0x090
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW  0x0a0
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH 0x0a4
#define VIRTIO_MMIO_CONFIG_GENERATION 0x0fc
#define VIRTIO_MMIO_CONFIG            0x100

#define VIRTIO_MMIO_MAGIC             0x74726976u
#define VIRTIO_MMIO_SLOT_SIZE         0x200

// `base` is the slot's registers, already mapped uncached. Fills `d`;
// -ENOENT for an empty slot, -EIO for a bad magic or a legacy
// (version 1) device.
int virtio_mmio_attach(volatile void* base, struct virtio_dev* d);

#endif
