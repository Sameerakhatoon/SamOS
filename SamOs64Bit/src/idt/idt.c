#include "idt.h"
#include "config.h"
#include "kernel.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"   // L37
#include "io/io.h"
// Lecture 52 - task subsystem is back in the build (L45). Re-
// enable the includes so the dispatch path's task_current /
// task_page / etc resolve.
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

// Lecture 38 - print and halt. The L38 smoke test deliberately
// triggers #DE via div_test in kernel.asm; this handler prints
// the marker and never returns (no iretq path back to the
// faulting instruction would make sense - the divide will
// fault again immediately).
void idt_zero(){
    print("Divide by zero error\n");
    while(1) {}
}

// Lecture 52 - panic for now. Future lecture will route through
// process_terminate / task_next once those calls are safer to
// make from interrupt context.
void idt_handle_exception(){
    panic("Panic Exception\n");
    // process_terminate(task_current()->process);
    // task_next();
}

// Lecture 52 - timer-IRQ callback. PIC ack + round-robin to the
// next task. The "test\n" print is upstream's debug
// breadcrumb; we keep it so the regression test can prove the
// IRQ wiring really fired once interrupts come up.
void idt_clock(){
    outb(0x20, 0x20);

    // Lecture 157 - the L140 window-test boot path runs without
    // any user task, so calling task_next would dereference NULL.
    // Bail when there is no current task to switch from.
    if(!task_current()){
        return;
    }

    task_next();
}

void no_interrupt_handler(){
    // Acknowledge the interrupt to the master PIC. Lecture 140
    // also EOIs the slave PIC so IRQ8..15 (which include the
    // PS/2 mouse on IRQ12) are not held permanently masked.
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}

// Lecture 52 - full task-aware handler restored.
//
// kernel_page() puts us on the kernel address space and reloads
// the kernel data selectors (we may have entered from a user
// task with its own PML4 + 0x33 data segs loaded).
//
// If a callback is registered for this vector, save the user's
// register state into the current task and fire it.
//
// task_page() restores the user task's PML4 + 0x33 data segs
// before iretq returns to ring 3.
//
// SAFETY: task_page() needs a current_task. As long as
// interrupts are disabled (no sti yet at L52), the timer
// never fires and the path is unreachable. The keyboard
// (L53) will need to make sure a task exists before enabling
// interrupts.
void interrupt_handler(int interrupt, struct interrupt_frame* frame){
    kernel_page();
    if(interrupt_callbacks[interrupt] != 0){
        // Lecture 157 - L140 commented the save/restore out so
        // the windowless boot would not crash. Now that user
        // tasks are coming back, gate the calls on
        // task_current() instead of skipping them outright.
        if(task_current()){
            task_current_save_state(frame);
        }
        interrupt_callbacks[interrupt](frame);
    }
    if(task_current()){
        task_page();
    }
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
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

// Lecture 52 - full task-aware syscall handler restored. Same
// shape as interrupt_handler: kernel_page in, dispatch, task_page
// out.
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

    // Lecture 52 - timer (IRQ0 = vector 0x20) now dispatches to
    // idt_clock which task_next's. Won't actually fire until we
    // sti somewhere.
    idt_register_interrupt_callback(0x20, idt_clock);

    idt_load(&idtr_descriptor);
}
