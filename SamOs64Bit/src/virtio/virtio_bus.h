#ifndef VIRTIO_BUS_H
#define VIRTIO_BUS_H
// Finding virtio devices and wiring their interrupts, whatever
// the transport: PCI functions (virtio_pci.c, x86) and the virtio-mmio
// slots the platform lists (QEMU virt on aarch64 and riscv64). The
// drivers (virtio-blk, virtio-input) only call virtio_probe and
// virtio_irq.
//
// A "line" is one completion slot of arch_irq_attach_gsi (x86: an
// IOAPIC route to a vector in 0x54-0x5B; aarch64: a GIC SPI taken in
// disk waits, EOI after the handler). Its handler reads every client's
// ISR (which acknowledges it and lets the line drop) and calls the
// client back. PCI INTx lines may be shared; an MMIO slot has its own.

#include <stdint.h>
#include <stdbool.h>
#include "virtio.h"

#define VIRTIO_LINE_CLIENTS     8

struct virtio_line {
    uint32_t gsi;
    uint16_t inti;               // MPS INTI flags (polarity, trigger)
    uint8_t  vector;             // arch_irq_attach_gsi's line number, for the log
    bool     masked;             // routed masked (samos.disk_irq_masked)
    int      clients;
    struct {
        struct virtio_dev* dev;
        void (*fn)(void* ctx);
        void* ctx;
    } client[VIRTIO_LINE_CLIENTS];
};

// Call `attach` for each device of type `device_id`, PCI first, then
// the MMIO slots in the order they were added. Returns how many
// attach accepted.
int virtio_probe(uint32_t device_id, int (*attach)(struct virtio_dev* d, void* ctx), void* ctx);

// Put `d` on its interrupt line, routing the line (masked if asked)
// when it is the first one there; `fn(ctx)` runs in the interrupt
// handler when the device's ISR shows a used-ring update. NULL without
// a route (no IOAPIC or GIC, a PCI function on another bus, no slot
// left).
struct virtio_line* virtio_irq(struct virtio_dev* d, bool masked, void (*fn)(void* ctx), void* ctx);

// A message-signalled interrupt for `d` instead of a line
// (virtio_pci_msix): the arch's line number, or < 0 for an MMIO slot or
// a function or platform without MSI-X.
struct cpu;
struct trap_frame;
int virtio_msix(struct virtio_dev* d, unsigned slot, struct cpu* c, void (*handler)(struct trap_frame* frame));

// The transports' half of virtio_irq: `d` on line `gsi`.
struct virtio_line* virtio_line_attach(struct virtio_dev* d, uint32_t gsi, uint16_t inti, bool masked,
                                       void (*fn)(void* ctx), void* ctx);

// A virtio-mmio slot: registers at `phys` (`size` bytes), interrupt
// `irq` (the controller's own number: a GIC INTID, a PLIC source) with
// MPS INTI `inti`. The arch adds them in arch_firmware_init, before
// the drivers probe. -ENOSPC past VIRTIO_MMIO_MAX_SLOTS.
#define VIRTIO_MMIO_MAX_SLOTS   32
int virtio_mmio_add_slot(uint64_t phys, uint64_t size, uint32_t irq, uint16_t inti);

#endif
