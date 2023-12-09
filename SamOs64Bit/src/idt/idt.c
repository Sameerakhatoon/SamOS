#include "idt.h"
#include "config.h"
#include "kernel.h"
#include "memory/memory.h"
#include "io/io.h"
#include "task/task.h"
#include "task/process.h"
#include "status.h"
#include "io/pit.h"
#include "io/apic.h"

// 256 gates; x86 has no more vectors (the book sized it 512).
struct idt_desc  idt_descriptors[SAMOS_TOTAL_INTERRUPTS];
struct idtr_desc idtr_descriptor;

extern void  idt_load(struct idtr_desc* ptr);
extern void  isr80h_wrapper();
extern void* interrupt_pointer_table[SAMOS_TOTAL_INTERRUPTS];

static ISR80H_COMMAND isr80h_commands[SAMOS_MAX_ISR80H_COMMANDS];
static INTERRUPT_CALLBACK_FUNCTION interrupt_callbacks[SAMOS_TOTAL_INTERRUPTS];

static const char* exception_names[32] = {
    "#DE", "#DB", "NMI", "#BP", "#OF", "#BR", "#UD", "#NM",
    "#DF", "#CSO", "#TS", "#NP", "#SS", "#GP", "#PF", "#15",
    "#MF", "#AC", "#MC", "#XM", "#VE", "#CP", "#22", "#23",
    "#24", "#25", "#26", "#27", "#HV", "#VC", "#SX", "#31",
};

static uint64_t read_cr2(){
    uint64_t v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

static uint64_t read_cr3(){
    uint64_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

// " name 0x0123456789abcdef" through print(), so it reaches COM1 and
// the screen.
static void print_reg(const char* name, uint64_t value){
    char buf[20];
    const char* digits = "0123456789abcdef";
    buf[0] = '0';
    buf[1] = 'x';
    for(int i = 0; i < 16; i++){
        buf[2 + i] = digits[(value >> (60 - 4 * i)) & 0xF];
    }
    buf[18] = 0;
    print(name);
    print(buf);
}

void idt_dump_frame(struct trap_frame* f){
    print_reg("  rip ", f->rip);  print_reg(" cs ", f->cs);   print_reg(" rflags ", f->rflags);
    print_reg("\n  rsp ", f->rsp); print_reg(" ss ", f->ss);  print_reg(" cr2 ", read_cr2());
    print_reg(" cr3 ", read_cr3());
    print_reg("\n  rax ", f->rax); print_reg(" rbx ", f->rbx); print_reg(" rcx ", f->rcx); print_reg(" rdx ", f->rdx);
    print_reg("\n  rsi ", f->rsi); print_reg(" rdi ", f->rdi); print_reg(" rbp ", f->rbp); print_reg(" r8  ", f->r8);
    print_reg("\n  r9  ", f->r9);  print_reg(" r10 ", f->r10); print_reg(" r11 ", f->r11); print_reg(" r12 ", f->r12);
    print_reg("\n  r13 ", f->r13); print_reg(" r14 ", f->r14); print_reg(" r15 ", f->r15);
    print("\n");
}

// SamOS divergence: the book panics on every exception
// (idt_handle_exception), so one bad user program halts the machine.
// A fault in ring 3 now kills only that process; a fault in the
// kernel, and #DF, NMI and #MC from anywhere, is a panic with a
// register dump. Vector 0 takes this path too: the book pointed it
// at a C function that spun forever.
static void idt_exception(struct trap_frame* frame, int from_user){
    uint64_t vector = frame->vector;
    int fatal = !from_user || vector == 2 || vector == 8 || vector == 18;

    if(from_user){
        kernel_page();
    }

    if(fatal){
        print("\nKERNEL FAULT: ");
        print(exception_names[vector]);
        print_reg(" vector ", vector);
        print_reg(" error ", frame->error_code);
        print("\n");
        idt_dump_frame(frame);
        panic("CPU exception in kernel mode\n");
    }

    struct process* process = task_current()->process;
    print("\nUSER FAULT: ");
    print(exception_names[vector]);
    print(" in ");
    print(process->filename);
    print_reg(": rip ", frame->rip);
    print_reg(" error ", frame->error_code);
    print_reg(" cr2 ", read_cr2());
    print("\nprocess killed\n");

    // The parent's wait sees 128 + the vector, the shell
    // convention for a process killed by a signal.
    process_exit(process, PROCESS_EXIT_FAULT_BASE + (int)vector);
    task_next();
}

// Lecture 52 - timer IRQ: round-robin to the next task. The EOI is
// sent by interrupt_handler before the callback runs, since
// task_next does not return.
// IRQ0 is unmasked now (PIT at 100 Hz). Count the tick,
// wake sleepers, preempt a ring-3 task if another one is ready; a
// tick in ring 0 (the idle loop) never switches here.
void idt_clock(struct trap_frame* frame){
    pit_tick();
    task_tick(frame);
}

// An IRQ7/IRQ15 with its in-service bit clear is a spurious
// interrupt from the 8259 and must not be acknowledged (a spurious
// IRQ15 still needs the master's EOI for the cascade).
static int pic_spurious(uint64_t vector){
    if(vector == IDT_IRQ_BASE + 7){
        outb(0x20, 0x0B);
        return !(insb(0x20) & 0x80);
    }
    if(vector == IDT_IRQ_SLAVE_BASE + 7){
        outb(0xA0, 0x0B);
        if(!(insb(0xA0) & 0x80)){
            outb(0x20, 0x20);
            return 1;
        }
    }
    return 0;
}

static void pic_eoi(uint64_t vector){
    if(vector >= IDT_IRQ_SLAVE_BASE){
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

// Every vector except 0x80 lands here (idt.asm trap_common).
//
// Restored from the 32-bit tree (the port lost it): only a trap
// from ring 3 switches to the kernel address space and saves the
// task's state. A trap taken in ring 0 is already on kernel CR3 and
// segments, and there is no user state to save.
//
// SamOS divergence: only the 16 PIC vectors get an EOI,
// sent before the callback because a callback may switch tasks and
// never return. The book sent EOI to both PICs on every vector.
void interrupt_handler(struct trap_frame* frame){
    uint64_t vector = frame->vector;
    int from_user = (frame->cs & 3) == 3;

    if(vector < 32){
        idt_exception(frame, from_user);
        return;
    }

    // With the APIC every vector from 32 up is a LAPIC
    // interrupt (int 0x80 has its own entry) and gets a LAPIC EOI,
    // except the LAPIC spurious vector and the masked 8259s' range,
    // where only a spurious 8259 IRQ can land.
    // A disk vector's EOI waits for its handler: the AHCI
    // line is level-triggered, and an EOI while the device still
    // asserts it delivers the interrupt again.
    bool late_eoi = false;
    if(apic_active()){
        if(vector == APIC_VECTOR_SPURIOUS ||
           (vector >= APIC_VECTOR_PIC_BASE && vector < APIC_VECTOR_PIC_END)){
            return;
        }
        late_eoi = vector >= APIC_VECTOR_DISK_BASE && vector < APIC_VECTOR_DISK_END;
        if(!late_eoi){
            lapic_eoi();
        }
    } else if(vector >= IDT_IRQ_BASE && vector < IDT_IRQ_END){
        if(pic_spurious(vector)){
            return;
        }
        pic_eoi(vector);
    }

    if(from_user){
        kernel_page();
        task_current_save_state(frame);
    }
    if(interrupt_callbacks[vector]){
        interrupt_callbacks[vector](frame);
    }
    if(late_eoi){
        lapic_eoi();
    }
    if(from_user){
        task_page();
    }
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
// SamOS divergence: every gate is DPL 0 (0x8E) except
// int 0x80 (0xEE), so ring 3 can raise only the syscall vector; the
// book made every vector from 0x32 up user-callable. `ist` picks a
// TSS stack for #DF, NMI and #MC.
static void idt_set(int interrupt_no, void* address, uint8_t type_attr, uint8_t ist){
    struct idt_desc* desc = &idt_descriptors[interrupt_no];
    uintptr_t _address = (uintptr_t)address;
    desc->offset_1  = (uint16_t)(_address         & 0xFFFF);
    desc->selector  = KERNEL_LONG_MODE_CODE_SELECTOR;
    desc->ist       = ist;
    desc->type_attr = type_attr;
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

void* isr80h_handle_command(int command, struct trap_frame* frame){
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

// Lecture 52 - syscall entry: kernel_page in, dispatch, task_page
// out. The command is the user's rax from the frame, and the
// result goes back through frame->rax instead of a global slot.
void isr80h_handler(struct trap_frame* frame){
    kernel_page();
    task_current_save_state(frame);
    frame->rax = (uint64_t)isr80h_handle_command((int)frame->rax, frame);
    task_page();
}

void idt_init(){
    memset(idt_descriptors, 0, sizeof(idt_descriptors));
    idtr_descriptor.limit = sizeof(idt_descriptors) - 1;
    idtr_descriptor.base  = (uint64_t)(uintptr_t)idt_descriptors;

    for(int i = 0; i < SAMOS_TOTAL_INTERRUPTS; i++){
        idt_set(i, interrupt_pointer_table[i], 0x8E, 0);
    }
    idt_set(2,    interrupt_pointer_table[2],  0x8E, IDT_IST_NMI);
    idt_set(8,    interrupt_pointer_table[8],  0x8E, IDT_IST_DOUBLE_FAULT);
    idt_set(18,   interrupt_pointer_table[18], 0x8E, IDT_IST_MACHINE_CHECK);
    idt_set(0x80, isr80h_wrapper, 0xEE, 0);

    // Lecture 52 - timer (IRQ0). kernel_main programs the PIT
    // and unmasks it.
    idt_register_interrupt_callback(IDT_IRQ_BASE, idt_clock);
    // The LAPIC timer, when kernel_main starts it.
    idt_register_interrupt_callback(APIC_VECTOR_TIMER, idt_clock);

    idt_load(&idtr_descriptor);
}
