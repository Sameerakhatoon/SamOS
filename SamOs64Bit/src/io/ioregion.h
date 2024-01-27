#ifndef IO_IOREGION_H
#define IO_IOREGION_H
// A window of device registers, in port I/O space or in
// memory, read and written through one handle (Linux's iomap idea),
// so a driver does not depend on the bus its device sits on (drivers never
// use in/out themselves). A PCI I/O BAR or a
// legacy ISA port range is a port region on x86; on aarch64 and
// riscv64 the host bridge maps PCI I/O space into memory, so the same
// BAR becomes an MMIO region there.
//
// Header only, so no Makefile change.

#include <stdint.h>
#include <stdbool.h>
#include "arch/arch.h"

struct io_region {
    uintptr_t base;    // port number, or kernel virtual address
    bool      port;
};

static inline struct io_region io_region_port(uint16_t port){
    struct io_region r = { port, true };
    return r;
}

static inline struct io_region io_region_mmio(volatile void* va){
    struct io_region r = { (uintptr_t)va, false };
    return r;
}

static inline uint8_t io_read8(struct io_region r, uint32_t off){
    return r.port ? arch_pio_read8((uint16_t)(r.base + off)) : mmio_read8((volatile void*)(r.base + off));
}

static inline uint16_t io_read16(struct io_region r, uint32_t off){
    return r.port ? arch_pio_read16((uint16_t)(r.base + off)) : mmio_read16((volatile void*)(r.base + off));
}

static inline uint32_t io_read32(struct io_region r, uint32_t off){
    return r.port ? arch_pio_read32((uint16_t)(r.base + off)) : mmio_read32((volatile void*)(r.base + off));
}

static inline void io_write8(struct io_region r, uint32_t off, uint8_t v){
    if(r.port){
        arch_pio_write8((uint16_t)(r.base + off), v);
    } else {
        mmio_write8((volatile void*)(r.base + off), v);
    }
}

static inline void io_write16(struct io_region r, uint32_t off, uint16_t v){
    if(r.port){
        arch_pio_write16((uint16_t)(r.base + off), v);
    } else {
        mmio_write16((volatile void*)(r.base + off), v);
    }
}

static inline void io_write32(struct io_region r, uint32_t off, uint32_t v){
    if(r.port){
        arch_pio_write32((uint16_t)(r.base + off), v);
    } else {
        mmio_write32((volatile void*)(r.base + off), v);
    }
}

#endif
