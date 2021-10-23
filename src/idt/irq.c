// Lecture 67 - IRQ enable / disable.
//
// The 8259 mask register lives at master 0x21 / slave 0xA1.
// Bit N of the mask = "IRQ N masked" - reading and writing the
// register manipulates which IRQs the PIC will deliver. The L61
// PIC remap set the initial mask to 0xFB on master (only IRQ2
// = cascade enabled) and 0xFF on slave (all masked); these
// helpers let later code (keyboard driver, etc) selectively
// turn IRQs on without re-doing the whole init sequence.

#include "idt/irq.h"
#include "io/io.h"

void IRQ_enable(IRQ irq){
    int port = IRQ_MASTER_PORT;
    IRQ relative_irq = irq;
    if(irq >= PIC_SLAVE_STARTING_IRQ){
        port = IRQ_SLAVE_PORT;
        relative_irq = irq - PIC_SLAVE_STARTING_IRQ;
    }
    // Read current mask, CLEAR the bit (enable the IRQ), write
    // it back. Bit clear = unmasked = delivered.
    int pic_value = insb(port);
    pic_value &= ~(1 << relative_irq);
    outb(port, pic_value);
}

void IRQ_disable(IRQ irq){
    int port = IRQ_MASTER_PORT;
    IRQ relative_irq = irq;
    if(irq >= PIC_SLAVE_STARTING_IRQ){
        port = IRQ_SLAVE_PORT;
        relative_irq = irq - PIC_SLAVE_STARTING_IRQ;
    }
    // Read current mask, SET the bit (disable the IRQ), write
    // it back.
    int pic_value = insb(port);
    pic_value |= (1 << relative_irq);
    outb(port, pic_value);
}
