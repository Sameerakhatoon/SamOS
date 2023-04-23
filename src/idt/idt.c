#include "idt.h"
#include "config.h"
#include "kernel.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"   // L37
#include "io/io.h"
#include "task/task.h"
#include "task/process.h"
#include "status.h"

struct idt_desc  idt_descriptors[SAMOS_TOTAL_INTERRUPTS];
struct idtr_desc idtr_descriptor;

extern void  idt_load(struct idtr_desc* ptr);
extern void  no_interrupt();
extern void  isr80h_wrapper();
extern void* interrupt_pointer_table[SAMOS_TOTAL_INTERRUPTS];

static ISR80H_COMMAND isr80h_commands[SAMOS_MAX_ISR80H_COMMANDS];
static INTERRUPT_CALLBACK_FUNCTION interrupt_callbacks[SAMOS_TOTAL_INTERRUPTS];

void idt_zero(){
    print("Divide by zero error\n");
}

void idt_handle_exception(){
    process_terminate(task_current()->process);
    task_next();
}

void idt_clock(){
    outb(0x20, 0x20);
    task_next();
}

void no_interrupt_handler(){
    // Acknowledge the interrupt to the master PIC.
    outb(0x20, 0x20);
}

void interrupt_handler(int interrupt, struct interrupt_frame* frame){
    int from_user = ((frame->cs & 0x3) == 0x3);

    // G05+G07+G08: only swap segments / save state / restore paging when the
    // trap came FROM user mode. For kernel-mode interrupts the segments and
    // CR3 are already kernel-side, and frame->ss / frame->esp are random
    // stack bytes (CPU only pushes them on a privilege-level change), so
    // writing them into task->registers corrupts the user task's selectors
    // and the subsequent task_return iretd #GPs on `mov ds, ax`.
    if(from_user){
        kernel_page();
    }
    if(interrupt_callbacks[interrupt] != 0){
        if(task_current()){
            if(from_user){
                task_current_save_state(frame);
            }
            interrupt_callbacks[interrupt](frame);
        }
    }
    if(from_user && task_current()){
        task_page();
    }
    outb(0x20, 0x20);
}

int idt_register_interrupt_callback(int interrupt, INTERRUPT_CALLBACK_FUNCTION interrupt_callback){
    if(interrupt < 0 || interrupt >= SAMOS_TOTAL_INTERRUPTS){
        return -EINVARG;
    }
    interrupt_callbacks[interrupt] = interrupt_callback;
    return 0;
}

// Lecture 37 - 64-bit gate install.
//
//   - 64-bit selector (KERNEL_LONG_MODE_CODE_SELECTOR = 0x18,
//     the L=1 code seg in kernel.asm's GDT).
//   - Address split into three offset_N fields covering bits
//     0..15, 16..31, 32..63.
//   - ist = 0 (no TSS stack switch; there is no TSS yet).
//   - type_attr = 0x8E for CPU exceptions / interrupts (vectors
//     0..0x31 in upstream's convention - covers all 32 CPU
//     vectors plus the first 18 PIC IRQs, slightly broader than
//     strictly necessary but harmless). 0xEE for everything else
//     (gives DPL=3 so user-mode int 0x80 syscalls don't #GP).
void idt_set(int interrupt_no, void* address){
    struct idt_desc* desc = &idt_descriptors[interrupt_no];
    uintptr_t _address = (uintptr_t)address;
    desc->offset_1  = (uint16_t)(_address         & 0xFFFF);
    desc->selector  = KERNEL_LONG_MODE_CODE_SELECTOR;
    desc->ist       = 0;
    desc->type_attr = 0xEE;
    if(interrupt_no <= 0x31){
        desc->type_attr = 0x8E;
    }
    desc->offset_2  = (uint16_t)((_address >> 16) & 0xFFFF);
    desc->offset_3  = (uint32_t)((_address >> 32) & 0xFFFFFFFF);
    desc->reserved  = 0;
}

void isr80h_register_command(int command_id, ISR80H_COMMAND command){
    if(command_id < 0 || command_id >= SAMOS_MAX_ISR80H_COMMANDS){
        panic("The command is out of bounds\n");
    }
    if(isr80h_commands[command_id]){
        panic("Your attempting to overwrite an existing command\n");
    }
    isr80h_commands[command_id] = command;
}

void* isr80h_handle_command(int command, struct interrupt_frame* frame){
    void* result = 0;
    if(command < 0 || command >= SAMOS_MAX_ISR80H_COMMANDS){
        return 0;
    }
    ISR80H_COMMAND command_func = isr80h_commands[command];
    if(!command_func){
        return 0;
    }
    result = command_func(frame);
    return result;
}

void* isr80h_handler(int command, struct interrupt_frame* frame){
    void* res = 0;
    kernel_page();
    task_current_save_state(frame);
    res = isr80h_handle_command(command, frame);
    task_page();
    return res;
}

void idt_init(){
    memset(idt_descriptors, 0, sizeof(idt_descriptors));
    idtr_descriptor.limit = sizeof(idt_descriptors) - 1;
    // L37 - 64-bit base. The IDT itself lives wherever the
    // compiler placed its static array; uintptr_t / uint64_t
    // covers the full kernel address space.
    idtr_descriptor.base  = (uint64_t)(uintptr_t)idt_descriptors;

    // Ch 112: every IDT vector now points at its macro-generated asm stub.
    for(int i = 0; i < SAMOS_TOTAL_INTERRUPTS; i++){
        idt_set(i, interrupt_pointer_table[i]);
    }

    idt_set(0, idt_zero);
    idt_set(0x80, isr80h_wrapper);

    // Ch 147: register the same handler for every CPU exception vector (0..0x1F)
    // so a faulting user process gets terminated cleanly instead of triple-faulting.
    for(int i = 0; i < 0x20; i++){
        idt_register_interrupt_callback(i, idt_handle_exception);
    }

    // Ch 150: IRQ0 (timer, vector 0x20) drives round-robin task switching.
    idt_register_interrupt_callback(0x20, idt_clock);

    idt_load(&idtr_descriptor);
}
