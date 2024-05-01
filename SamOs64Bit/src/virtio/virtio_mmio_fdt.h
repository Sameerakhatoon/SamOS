#ifndef VIRTIO_MMIO_FDT_H
#define VIRTIO_MMIO_FDT_H
// virtio-mmio slots from the device tree, for arch_firmware_init
// on the ports (virtio_mmio_fdt.c).

#include <stdint.h>
#include <stdbool.h>
#include "firmware/fdt.h"

// Add every "virtio,mmio" node's slot (virtio_mmio_add_slot), sorted
// by address: highest first when `highest_first`, else lowest first.
// QEMU's arm virt plugs the first -device virtio-*-device into the
// highest slot (hw/arm/virt.c creates the transports so that its bus
// list runs downwards), so highest first makes command-line order the
// disk order. `irq_of` gives a node's interrupt in the controller's
// numbering and its MPS INTI flags, or < 0. Returns the slots added.
int virtio_mmio_scan_fdt(const struct fdt* f, int (*irq_of)(const struct fdt* f, int node, uint16_t* inti),
                         bool highest_first);

#endif
