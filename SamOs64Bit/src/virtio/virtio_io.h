#ifndef VIRTIO_IO_H
#define VIRTIO_IO_H
// What the virtio code needs from the platform, in one place:
// MMIO accessors, page-sized DMA memory with its physical address, and
// a full memory barrier.
//
// The hardware layer's arch.h is meant to provide mmio_read32/mmio_write32 for every
// architecture; until it exists these are local. When it lands, drop
// the accessor bodies below and include arch/arch.h instead: the names
// and argument order are the same (address first, then value).
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
#else
#include "memory/heap/kheap.h"
#include "memory/paging/paging.h"

static inline uint8_t  mmio_read8(volatile void* a)  { return *(volatile uint8_t*)a; }
static inline uint16_t mmio_read16(volatile void* a) { return *(volatile uint16_t*)a; }
static inline uint32_t mmio_read32(volatile void* a) { return *(volatile uint32_t*)a; }
static inline void mmio_write8(volatile void* a, uint8_t v)   { *(volatile uint8_t*)a = v; }
static inline void mmio_write16(volatile void* a, uint16_t v) { *(volatile uint16_t*)a = v; }
static inline void mmio_write32(volatile void* a, uint32_t v) { *(volatile uint32_t*)a = v; }

// One zeroed 4 KiB page. kpage_zalloc pages are page aligned, so a
// structure that fits in one never crosses a physical page boundary;
// multi-page buffers are translated page by page.
static inline void*    virtio_dma_page(void)        { return kpage_zalloc(4096); }
static inline void     virtio_dma_free(void* page)  { kfree(page); }
static inline uint64_t virtio_dma_phys(void* virt)  { return paging_kernel_virt_to_phys(virt); }
#endif

// The rings are shared with the device: order every ring write before
// the index that publishes it, and the index before the notify. The
// GCC builtin is mfence on x86-64, dmb ish on arm64, fence rw,rw on
// riscv64.
#define virtio_mb() __sync_synchronize()

#endif
