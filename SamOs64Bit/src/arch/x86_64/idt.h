#ifndef IDT_H
#define IDT_H

#include <stdint.h>
#include "arch/arch.h"     // Struct trap_frame (arch_impl.h)

int   idt_register_interrupt_callback(int interrupt, INTERRUPT_CALLBACK_FUNCTION interrupt_callback);

// Lecture 37 - 64-bit IDT gate descriptor (16 bytes).
//   ist        bits 0..2: TSS IST slot to switch to, 0 = none
//   type_attr  present + DPL + gate type
struct idt_desc {
    uint16_t offset_1;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_2;
    uint32_t offset_3;
    uint32_t reserved;
} __attribute__((packed));

struct idtr_desc {
    uint16_t limit;
    uint64_t base;   // L37 - widened to 64 bits
} __attribute__((packed));

// TSS interrupt stack table slots (tss.h ist1..ist3).
#define IDT_IST_DOUBLE_FAULT   1
#define IDT_IST_NMI            2
#define IDT_IST_MACHINE_CHECK  3

// The 8259 vectors after the kernel.asm remap.
#define IDT_IRQ_BASE           0x20
#define IDT_IRQ_SLAVE_BASE     0x28
#define IDT_IRQ_END            0x30

void idt_init();
void idt_load_cpu(void);
void enable_interrupts();
void disable_interrupts();
void idt_dump_frame(struct trap_frame* frame);

#endif
