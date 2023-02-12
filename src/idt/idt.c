#include "idt.h"
#include "config.h"
#include "kernel.h"
#include "memory/memory.h"
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
    kernel_page();
    if(interrupt_callbacks[interrupt] != 0){
        // G07: book unconditionally calls task_current_save_state which panics
        // on null current_task. With idt_clock registered for IRQ0 (Ch 150)
        // PIT IRQs that fire between enable_interrupts and the first
        // task_run_first_ever_task hit that panic. Skip the save (and the
        // callback) when there's no task to switch.
        if(task_current()){
            task_current_save_state(frame);
            interrupt_callbacks[interrupt](frame);
        }
    }
    // G05: book code calls task_page() unconditionally; that's task_switch(
    // current_task=NULL) when any IRQ fires before task_run_first_ever_task,
    // which triple-faults the kernel. Only swap back to user paging if a
    // task actually exists.
    if(task_current()){
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

void idt_set(int interrupt_no, void* address){
    struct idt_desc* desc = &idt_descriptors[interrupt_no];
    desc->offset_1  = (uint32_t)address & 0x0000FFFF;
    desc->selector  = KERNEL_CODE_SELECTOR;
    desc->zero      = 0x00;
    desc->type_attr = 0xEE;
    desc->offset_2  = (uint32_t)address >> 16;
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
    idtr_descriptor.base  = (uint32_t)idt_descriptors;

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
