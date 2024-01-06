#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// One frame for every trap, built by idt.asm. Lowest
// address first: the 15 general registers, then the vector and error
// code the stub pushed (0 when the CPU pushes none), then what the CPU
// pushes in long mode (always ss:rsp too, even without a ring change).
// It replaces the book's struct interrupt_frame and struct registers:
// a task's saved user state is a trap_frame as well.
struct trap_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

// The syscall convention, System V style on int 0x80:
// rax = command, arguments in rdi, rsi, rdx, r10, r8, r9 (r10, not
// rcx, as in the Linux syscall ABI, so the trampolines stay the same
// for a later SYSCALL instruction), result in rax. Every other
// register is preserved. The book pushed arguments on the user stack
// and the kernel read them back with task_get_stack_item.
static inline void* isr80h_arg(struct trap_frame* frame, int n){
    switch(n){
        case 0:  return (void*)frame->rdi;
        case 1:  return (void*)frame->rsi;
        case 2:  return (void*)frame->rdx;
        case 3:  return (void*)frame->r10;
        case 4:  return (void*)frame->r8;
        default: return (void*)frame->r9;
    }
}

typedef void* (*ISR80H_COMMAND)(struct trap_frame* frame);
typedef void  (*INTERRUPT_CALLBACK_FUNCTION)(struct trap_frame* frame);

void  isr80h_register_command(int command_id, ISR80H_COMMAND command);
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

// TSS interrupt stack table slots (task/tss.h ist1..ist3).
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
