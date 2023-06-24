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

enum {
    IRQ_TIMER,             // 0
    IRQ_KEYBOARD,          // 1
    IRQ_CASCADE,           // 2 - slave PIC's cascade line
    IRQ_COM2,              // 3
    IRQ_COM1,              // 4
    IRQ_LPT2,              // 5
    IRQ_FLOPPY_DISK,       // 6
    IRQ_LPT1,              // 7
    IRQ_CMOS,              // 8 - on slave
    IRQ_LEGACY_SCSI,       // 9
    IRQ_SCSI,              // 10
    IRQ_PS2_MOUSE,         // 11
    IRQ_FPU_COPROCESSOR,   // 12
    IRQ_PRIMARY_ATA_HDD,   // 13
    IRQ_SECONDARY_ATA_HDD, // 14
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
