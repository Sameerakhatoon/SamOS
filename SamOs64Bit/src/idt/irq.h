#ifndef KERNEL_IRQ
#define KERNEL_IRQ

// Lecture 67 - IRQ enable/disable C API.
//
// The 8259 master PIC handles IRQ0..IRQ7; the slave PIC handles
// IRQ8..IRQ15 and cascades through master IRQ2. The mask bit
// for an IRQ lives in:
//   master: port 0x21, bit IRQ
//   slave:  port 0xA1, bit (IRQ - 8)
// where bit set = masked (disabled), bit clear = unmasked
// (enabled).

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

#define PIC_MASTER_ENDING_IRQ   7
#define PIC_SLAVE_STARTING_IRQ  8
#define PIC_SLAVE_ENDING_IRQ    15
#define IRQ_MASTER_PORT         0x21    // master data port
#define IRQ_SLAVE_PORT          0xA1    // slave data port

typedef int IRQ;

void IRQ_enable(IRQ irq);
void IRQ_disable(IRQ irq);

#endif
