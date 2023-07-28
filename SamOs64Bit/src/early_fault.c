// Early exception handler.
//
// SamOS divergence from the book: kernel.asm installs a minimal
// 32-vector IDT before it swaps the GDT, so a CPU exception before
// idt_init() (or inside it) prints a register dump on COM1 and
// halts, instead of hitting the firmware's IDT and resetting the
// machine silently. idt_init() replaces this IDT later.

#include <stdint.h>
#include "io/serial.h"

// Pushed by early_fault_common in kernel.asm, lowest address first.
struct early_fault_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

static void dump(const char* name, uint64_t value)
{
    serial_write(name);
    serial_write_hex(value);
}

void early_fault(struct early_fault_frame* f)
{
    uint64_t cr2, cr3;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    serial_init();
    serial_write("\nEARLY FAULT: vector ");
    serial_write_hex(f->vector);
    dump(" error ", f->error_code);
    dump("\n  rip ", f->rip);  dump(" cs ", f->cs);   dump(" rflags ", f->rflags);
    dump("\n  rsp ", f->rsp);  dump(" ss ", f->ss);   dump(" cr2 ", cr2); dump(" cr3 ", cr3);
    dump("\n  rax ", f->rax);  dump(" rbx ", f->rbx); dump(" rcx ", f->rcx); dump(" rdx ", f->rdx);
    dump("\n  rsi ", f->rsi);  dump(" rdi ", f->rdi); dump(" rbp ", f->rbp); dump(" r8  ", f->r8);
    dump("\n  r9  ", f->r9);   dump(" r10 ", f->r10); dump(" r11 ", f->r11); dump(" r12 ", f->r12);
    dump("\n  r13 ", f->r13);  dump(" r14 ", f->r14); dump(" r15 ", f->r15);
    serial_write("\nSystem halted.\n");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
