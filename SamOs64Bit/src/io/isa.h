#ifndef IO_ISA_H
#define IO_ISA_H

#include <stdint.h>
#include "io/ioregion.h"
// ISA interrupt lines of the PC platform: what the drivers of
// PC devices (PS/2, legacy ATA, CMOS clock) pass to arch_irq_register
// and arch_irq_unmask. Moved out of the 8259 header, irq.h.
//
// SamOS divergence: the book's enum skips IRQ 9, so
// every slave line from there up is one low (the mouse came out as
// 11 instead of 12). The numbers are now explicit.
enum {
    IRQ_TIMER             = 0,
    IRQ_KEYBOARD          = 1,
    IRQ_CASCADE           = 2,  // slave PIC's cascade line
    IRQ_COM2              = 3,
    IRQ_COM1              = 4,
    IRQ_LPT2              = 5,
    IRQ_FLOPPY_DISK       = 6,
    IRQ_LPT1              = 7,
    IRQ_CMOS              = 8,  // RTC, first slave line
    IRQ_ACPI              = 9,  // free / ACPI SCI
    IRQ_LEGACY_SCSI       = 10, // free / SCSI / NIC
    IRQ_SCSI              = 11, // free / SCSI / NIC
    IRQ_PS2_MOUSE         = 12,
    IRQ_FPU_COPROCESSOR   = 13,
    IRQ_PRIMARY_ATA_HDD   = 14,
    IRQ_SECONDARY_ATA_HDD = 15,
};

// One register of a PC platform device (PS/2 controller, CMOS
// clock) at a fixed ISA I/O port, through io/ioregion.h.
static inline uint8_t isa_read8(uint16_t port){
    return io_read8(io_region_port(port), 0);
}

static inline void isa_write8(uint16_t port, uint8_t v){
    io_write8(io_region_port(port), 0, v);
}

#endif
