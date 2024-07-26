#ifndef VIRTIO_PCI_H
#define VIRTIO_PCI_H
// Modern virtio-pci transport (virtio 1.x, spec 4.1).
//
// Vendor 0x1AF4: device 0x1040 + type (modern only) or 0x1000-0x103F
// (transitional, type in the subsystem id). Only the vendor-specific
// capabilities are used (common, notify, ISR, device config), through
// the BAR they name, mapped uncached; the legacy I/O BAR of a
// transitional device is never touched, so there is no in/out here.
//
// Interrupts: PCI INTx (pci_intx_gsi). Each GSI a virtio function
// arrives on is a "line" (virtio_bus.h).
// MSI-X is not used by the disk drivers: QEMU offers it, but INTx is
// what AHCI and PATA already do, and the disk wait steers a line by its
// GSI. virtio-net takes it (virtio_pci_msix). A function on
// a bus other than 0, or a chipset pci_intx_gsi does not know, gets no
// line and its driver polls.

#include <stdint.h>
#include <stdbool.h>
#include "virtio.h"
#include "virtio_bus.h"

#define VIRTIO_PCI_VENDOR       0x1AF4

// Call `attach` for each virtio function of type `device_id`, set up
// and with its capabilities mapped. Returns how many attach accepted.
int virtio_pci_probe(uint32_t device_id, int (*attach)(struct virtio_dev* d, void* ctx), void* ctx);

// Put `d` on its INTx line, routing the line (masked if asked) when it
// is the first one there; `fn(ctx)` runs in the interrupt handler when
// the device's ISR shows a used-ring update. NULL without a route.
struct virtio_line* virtio_pci_intx(struct virtio_dev* d, bool masked, void (*fn)(void* ctx), void* ctx);

// One MSI-X message for the whole function (table entry 0, every
// queue on vector 0, no configuration interrupt), sent to CPU `c` on
// completion slot `slot` (arch_irq_msi). After virtio_negotiate (its
// reset clears the vectors) and before the queues are set up. The
// arch's line number, or < 0 without MSI-X (the caller takes INTx).
struct cpu;
struct trap_frame;
int virtio_pci_msix(struct virtio_dev* d, unsigned slot, struct cpu* c, void (*handler)(struct trap_frame* frame));

#endif
