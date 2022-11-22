#ifndef IDT_H
#define IDT_H

#include <stdint.h>

struct interrupt_frame {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t reserved;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    // The next five fields are pushed automatically by the CPU on a
    // ring-3 -> ring-0 trap, before our isr80h_wrapper runs.
    uint32_t ip;
    uint32_t cs;
    uint32_t flags;
    uint32_t esp;
    uint32_t ss;
} __attribute__((packed));

struct idt_desc {
    uint16_t offset_1;     // bits 0..15 of handler address
    uint16_t selector;     // GDT selector for the handler's code segment
    uint8_t  zero;         // reserved, set to 0
    uint8_t  type_attr;    // present, DPL, gate type
    uint16_t offset_2;     // bits 16..31 of handler address
} __attribute__((packed));

struct idtr_desc {
    uint16_t limit;        // sizeof(idt) - 1
    uint32_t base;         // linear address of the IDT
} __attribute__((packed));

void idt_init();
void enable_interrupts();
void disable_interrupts();

#endif
