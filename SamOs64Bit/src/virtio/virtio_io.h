#ifndef VIRTIO_IO_H
#define VIRTIO_IO_H
// What the virtio code needs from the platform, in one place:
// the arch.h MMIO accessors, page-sized DMA memory with its physical
// address, and a full memory barrier.
//
// VIRTIO_HOST_TEST (tests64/host/virtio_test.c) turns every accessor
// into an extern function, so a fake device can watch each register
// access, and the DMA helpers into plain host memory.

#include <stdint.h>

#ifdef VIRTIO_HOST_TEST
uint8_t  mmio_read8(volatile void* addr);
uint16_t mmio_read16(volatile void* addr);
uint32_t mmio_read32(volatile void* addr);
void     mmio_write8(volatile void* addr, uint8_t v);
void     mmio_write16(volatile void* addr, uint16_t v);
void     mmio_write32(volatile void* addr, uint32_t v);
void*    virtio_dma_page(void);
void     virtio_dma_free(void* page);
uint64_t virtio_dma_phys(void* virt);
#define virtio_mb() __sync_synchronize()
#else
#include "arch/arch.h"
#include "memory/heap/kheap.h"

// One zeroed 4 KiB page. kpage_zalloc pages are page aligned, so a
// structure that fits in one never crosses a physical page boundary;
// multi-page buffers are translated page by page.
static inline void*    virtio_dma_page(void)        { return kpage_zalloc(4096); }
static inline void     virtio_dma_free(void* page)  { kfree(page); }
static inline uint64_t virtio_dma_phys(void* virt)  { return paging_kernel_virt_to_phys(virt); }

// The rings are shared with the device: order every ring write before
// the index that publishes it, and the index before the notify.
#define virtio_mb() smp_mb()
#endif

#endif
