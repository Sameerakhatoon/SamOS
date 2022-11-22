#include "idt.h"
#include "config.h"
#include "kernel.h"
#include "memory/memory.h"
#include "io/io.h"
#include "task/task.h"

struct idt_desc  idt_descriptors[SAMOS_TOTAL_INTERRUPTS];
struct idtr_desc idtr_descriptor;

extern void idt_load(struct idtr_desc* ptr);
extern void int21h();
extern void no_interrupt();

void idt_zero(){
    print("Divide by zero error\n");
}

void int21h_handler(){
    // G01: drain the keyboard controller's data port. Without this read,
    // port 0x60 holds the previous scancode and the controller never raises
    // IRQ1 again, so only the first key produces output.
    unsigned char sc = insb(0x60);

    // G02: only print on key DOWN, not key UP. The PS/2 keyboard sends one
    // scancode on press (high bit clear) and another on release (high bit
    // set). Filtering out the release halves the output so each key produces
    // exactly one "Keyboard pressed!" line. Extended-prefix bytes (0xE0)
    // are also filtered by this check because we read them on a separate
    // IRQ and the next scancode comes on the IRQ after that.
    if((sc & 0x80) == 0){
        print("Keyboard pressed!\n");
    }

    // Acknowledge the interrupt to the master PIC.
    outb(0x20, 0x20);
}

void no_interrupt_handler(){
    // Acknowledge the interrupt to the master PIC.
    outb(0x20, 0x20);
}

void idt_set(int interrupt_no, void* address){
    struct idt_desc* desc = &idt_descriptors[interrupt_no];
    desc->offset_1  = (uint32_t)address & 0x0000FFFF;
    desc->selector  = KERNEL_CODE_SELECTOR;
    desc->zero      = 0x00;
    desc->type_attr = 0xEE;
    desc->offset_2  = (uint32_t)address >> 16;
}

void* isr80h_handle_command(int command, struct interrupt_frame* frame){
    // Real command dispatch lands in Ch 103+. For now this is a stub
    // so the wrapper has a callable target.
    return 0;
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

    // Point every vector at the no-op handler to start with.
    for(int i = 0; i < SAMOS_TOTAL_INTERRUPTS; i++){
        idt_set(i, no_interrupt);
    }

    idt_set(0, idt_zero);
    idt_set(0x21, int21h);

    idt_load(&idtr_descriptor);
}
