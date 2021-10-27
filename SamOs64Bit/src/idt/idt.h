#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// Lecture 37 - 64-bit interrupt frame. The CPU now pushes 8-byte
// ip/cs/flags/sp/ss on EVERY interrupt (unlike 32-bit, which
// skipped ss/sp on same-DPL traps). pushad_macro inside the asm
// stub adds the GPR block at the front in the same order the
// 32-bit pushad used.
struct interrupt_frame {
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t reserved;   // 32-bit pushad pushed esp here; we keep
                         // the slot occupied with a placeholder
                         // so the struct stride matches what
                         // pushad_macro pushes
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rax;

    // CPU-pushed frame (always 8-byte in long mode):
    uint64_t ip;
    uint64_t cs;
    uint64_t flags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

typedef void* (*ISR80H_COMMAND)(struct interrupt_frame* frame);
typedef void  (*INTERRUPT_CALLBACK_FUNCTION)(struct interrupt_frame* frame);

void  isr80h_register_command(int command_id, ISR80H_COMMAND command);
int   idt_register_interrupt_callback(int interrupt, INTERRUPT_CALLBACK_FUNCTION interrupt_callback);

// Lecture 37 - 64-bit IDT gate descriptor. Was 8 bytes; now 16:
//   offset_1   16 bits  handler address bits 0..15
//   selector   16 bits  GDT selector (kernel long-mode code seg)
//   ist         3 bits  (in the low byte) - which TSS stack to
//                       switch to; 0 = use the current one. We
//                       leave it 0 for now (no TSS stack table
//                       set up yet). The remaining 5 bits of
//                       this byte are reserved zero, hence the
//                       full uint8_t field.
//   type_attr   8 bits  present + DPL + gate-type
//   offset_2   16 bits  bits 16..31
//   offset_3   32 bits  bits 32..63
//   reserved   32 bits  must be zero
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

void idt_init();
void enable_interrupts();
void disable_interrupts();

#endif
