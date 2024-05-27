// Exception and IRQ dispatch (see trap.h, vectors.S).
//
// Synchronous exceptions at EL1 decode ESR_EL1 and dump the frame;
// only an SVC with a handler installed returns. IRQs are acknowledged
// at the GIC, handed to the INTID's handler, ended, and then the
// scheduler may pick another thread's frame to resume.
//
// Once kernel_main's arch_trap_init has run (trap_tasks), traps
// follow the shared kernel's model, x86's interrupt_handler:
//   - svc from EL0: isr80h_handler (it saves the task's state, runs the
//     syscall, puts the result in x0);
//   - any other sync exception from EL0: the process is killed with
//     ESR, ELR and FAR on the console, the CPU runs the next task;
//   - IRQ: EOI first (the handler may switch tasks and not come back),
//     then the handler (device completion lines EOI after it); from EL0 the task's state is saved before and
//     task_check_killed runs after;
//   - a data abort at EL1 on a user access listed in __ex_table
//     (uaccess.S) resumes at its fixup.
#include "arch/aarch64/trap.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/gicv3.h"
#include "arch/aarch64/sched.h"
#include "arch/aarch64/sysreg.h"
#include "arch/arch.h"
#include "cpu/smp.h"
#include "kernel.h"
#include "isr80h/isr80h.h"
#include "task/task.h"
#include "task/process.h"

#define IRQ_MAX 1020            // INTIDs 0-1019: SGI, PPI, SPI
#define IRQ_SLOTS (IRQ_MAX + GIC_LPIS)  // S-PAR: then the LPIs (its.c)

// ESR_EL1.EC, bits [31:26]
#define EC_UNKNOWN      0x00    // also UDF and other undefined encodings
#define EC_WFX          0x01
#define EC_SVC64        0x15
#define EC_SYSREG       0x18    // MSR/MRS/system instruction not allowed here
#define EC_IABT_LOWER   0x20
#define EC_IABT_CUR     0x21
#define EC_PC_ALIGN     0x22
#define EC_DABT_LOWER   0x24
#define EC_DABT_CUR     0x25
#define EC_SP_ALIGN     0x26
#define EC_BRK64        0x3c

static irq_handler irq_handlers[IRQ_SLOTS];
static uint32_t irq_late_eoi[IRQ_SLOTS / 32 + 1];  // EOI after the handler

// An INTID's entry in the tables, -1 for none.
static int irq_slot(uint32_t intid)
{
    if (intid < IRQ_MAX)
        return (int)intid;
    return intid - GIC_INTID_LPI < GIC_LPIS ? (int)(IRQ_MAX + intid - GIC_INTID_LPI) : -1;
}

static void (*svc_handler)(struct trap_frame* f, uint32_t imm);
static int trap_tasks;

void aarch64_exception(uint64_t vector, uint64_t esr, uint64_t elr, uint64_t far, uint64_t spsr);
void aarch64_fiq(void) __attribute__((noreturn));
void aarch64_bad_stack(uint64_t esr, uint64_t elr, uint64_t far, uint64_t spsr, uint64_t sp);

struct ex_entry {
    uint64_t insn, fixup;
};
extern const struct ex_entry __ex_table_start[], __ex_table_end[];

static const char* const vector_names[4] = { "sync", "irq", "fiq", "serror" };
static const char* const vector_groups[4] = {
    "current EL, SP_EL0", "current EL, SP_ELx", "lower EL, AArch64", "lower EL, AArch32",
};

void aarch64_halt(void)
{
    for (;;)
        __asm__ volatile("msr daifset, #0xf\n\twfi");
}

static const char* ec_name(uint32_t ec)
{
    switch (ec) {
    case EC_UNKNOWN:    return "undefined instruction";
    case EC_WFX:        return "wfi/wfe";
    case EC_SVC64:      return "svc";
    case EC_SYSREG:     return "system register access";
    case EC_IABT_LOWER: return "instruction abort, lower EL";
    case EC_IABT_CUR:   return "instruction abort";
    case EC_PC_ALIGN:   return "pc alignment fault";
    case EC_DABT_LOWER: return "data abort, lower EL";
    case EC_DABT_CUR:   return "data abort";
    case EC_SP_ALIGN:   return "sp alignment fault";
    case EC_BRK64:      return "brk";
    default:            return "other";
    }
}

// Data/instruction fault status code, ISS bits [5:0].
static const char* fsc_name(uint32_t fsc)
{
    if (fsc <= 0x03) return "address size fault";
    if (fsc <= 0x07) return "translation fault";
    if (fsc <= 0x0b) return "access flag fault";
    if (fsc <= 0x0f) return "permission fault";
    if (fsc == 0x10) return "synchronous external abort";
    if (fsc == 0x21) return "alignment fault";
    return "other";
}

static void print_cause(const struct trap_frame* f)
{
    uint32_t ec = (f->esr >> 26) & 0x3f;
    uint32_t iss = f->esr & 0x1ffffff;
    switch (ec) {
    case EC_UNKNOWN:
        // ELR is in the kernel text, which is readable.
        console_printf("  cause: undefined instruction 0x%08x\n", *(const volatile uint32_t*)f->elr);
        break;
    case EC_SVC64:
    case EC_BRK64:
        console_printf("  cause: %s #0x%x\n", ec_name(ec), iss & 0xffff);
        break;
    case EC_DABT_LOWER:
    case EC_DABT_CUR:
        console_printf("  cause: %s, %s, DFSC 0x%x (%s), FAR 0x%lx\n", ec_name(ec),
                       (iss >> 6) & 1 ? "write" : "read", iss & 0x3f, fsc_name(iss & 0x3f), f->far);
        break;
    case EC_IABT_LOWER:
    case EC_IABT_CUR:
        console_printf("  cause: %s, IFSC 0x%x (%s), FAR 0x%lx\n", ec_name(ec), iss & 0x3f,
                       fsc_name(iss & 0x3f), f->far);
        break;
    default:
        console_printf("  cause: %s\n", ec_name(ec));
        break;
    }
}

void trap_dump(const struct trap_frame* f, uint64_t vector)
{
    uint32_t ec = (f->esr >> 26) & 0x3f;
    console_printf("\nexception: %s (%s)\n", vector_names[vector & 3], vector_groups[(vector >> 2) & 3]);
    console_printf("  ESR 0x%lx (EC 0x%x, %s)\n", f->esr, ec, ec_name(ec));
    console_printf("  ELR 0x%lx\n  FAR 0x%lx\n  SPSR 0x%lx\n", f->elr, f->far, f->spsr);
    print_cause(f);
    for (int i = 0; i < 31; i++) {
        console_printf("  x%d%s 0x%016lx", i, i < 10 ? " " : "", f->x[i]);
        if (i % 3 == 2 || i == 30)
            console_printf("\n");
    }
    console_printf("  sp  0x%016lx\n", f->sp);
}

static const struct ex_entry* ex_lookup(uint64_t elr)
{
    for (const struct ex_entry* e = __ex_table_start; e < __ex_table_end; e++)
        if (e->insn == elr)
            return e;
    return 0;
}

static struct trap_frame* sync_el1(struct trap_frame* f)
{
    uint32_t ec = (f->esr >> 26) & 0x3f;
    if (ec == EC_SVC64 && svc_handler) {
        svc_handler(f, f->esr & 0xffff);   // ELR is already past the svc
        return f;
    }
    const struct ex_entry* e = ec == EC_DABT_CUR ? ex_lookup(f->elr) : 0;
    if (e) {
        f->elr = e->fixup;
        return f;
    }
    trap_dump(f, VECTOR_EL1H_SYNC);
    console_printf("exception: halted\n");
    aarch64_halt();
}

static char* put_str(char* p, const char* s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static char* put_hex(char* p, uint64_t v)
{
    p = put_str(p, "0x");
    int shift = 60;
    while (shift > 0 && !((v >> shift) & 0xf))
        shift -= 4;
    for (; shift >= 0; shift -= 4)
        *p++ = "0123456789abcdef"[(v >> shift) & 0xf];
    return p;
}

// The exit status of a process a fault kills is 128 + a trap class in
// x86's numbering, so a parent sees the same status for the same kind
// of fault on every arch: 14 for an abort (#PF), 6 for an undefined
// instruction (#UD), 17 for an alignment fault (#AC), 3 for brk (#BP),
// 13 (#GP) for the rest (a system register or wfi at EL0, ...).
static int fault_class(uint32_t ec)
{
    switch (ec) {
    case EC_IABT_LOWER:
    case EC_DABT_LOWER: return 14;
    case EC_UNKNOWN:    return 6;
    case EC_PC_ALIGN:
    case EC_SP_ALIGN:   return 17;
    case EC_BRK64:      return 3;
    default:            return 13;
    }
}

// As x86 does for a fault in ring 3: report, end the process,
// run the next task.
static void user_fault(struct trap_frame* f)
{
    uint32_t ec = (f->esr >> 26) & 0x3f;
    kernel_page();
    struct process* process = task_current()->process;
    char line[256];
    char* p = put_str(line, "\nUSER FAULT: ");
    p = put_str(p, ec_name(ec));
    p = put_str(p, " in ");
    p = put_str(p, process->filename);
    p = put_str(p, ": ESR ");
    p = put_hex(p, f->esr);
    p = put_str(p, " ELR ");
    p = put_hex(p, f->elr);
    p = put_str(p, " FAR ");
    p = put_hex(p, f->far);
    put_str(p, "\nprocess killed\n")[0] = 0;
    print(line);
    process_exit(process, PROCESS_EXIT_FAULT_BASE + fault_class(ec));
    task_next();
}

static struct trap_frame* sync_el0(struct trap_frame* f)
{
    if (((f->esr >> 26) & 0x3f) == EC_SVC64) {
        isr80h_handler(f);
        return f;
    }
    user_fault(f);
    return f;
}

static struct trap_frame* irq(struct trap_frame* f)
{
    uint32_t intid = gic_ack();
    if (intid >= GIC_INTID_SPURIOUS && intid < GIC_INTID_LPI)
        return f;                           // nothing pending: no EOI
    int slot = irq_slot(intid);
    irq_handler h = slot >= 0 ? irq_handlers[slot] : 0;
    if (!trap_tasks) {
        if (h)
            h(f);
        else
            console_printf("irq: unexpected INTID %u\n", intid);
        gic_eoi(intid);
        return sched_switch(f);
    }
    // A device completion line (arch_irq_attach_gsi) ends after
    // its handler quietened the device; its handler always returns.
    bool late = slot >= 0 && (irq_late_eoi[slot / 32] >> (slot % 32)) & 1;
    if (!late)
        gic_eoi(intid);
    bool user = trap_frame_from_user(f);
    if (user) {
        kernel_page();
        task_current_save_state(f);
    }
    if (h)
        h(f);
    else
        console_printf("irq: unexpected INTID %u\n", intid);
    if (late)
        gic_eoi(intid);
    if (user) {
        task_check_killed();
        task_page();
    }
    return f;
}

struct trap_frame* aarch64_trap(struct trap_frame* f, uint64_t vector)
{
    struct trap_frame* next;
    switch (vector) {
    case VECTOR_EL1H_SYNC: next = sync_el1(f); break;
    case VECTOR_EL0_SYNC:  next = sync_el0(f); break;
    default:               next = irq(f); break;    // VECTOR_EL1H_IRQ, VECTOR_EL0_IRQ
    }
    // vectors.S puts SP just above the frame for an EL1 return.
    if ((next->spsr & 0xf) == SPSR_EL1H && next->sp != (uint64_t)next + TRAP_FRAME_SIZE) {
        console_printf("trap: frame at 0x%lx has sp 0x%lx\n", (uint64_t)next, next->sp);
        aarch64_halt();
    }
    return next;
}

int irq_register(uint32_t intid, irq_handler handler)
{
    int slot = irq_slot(intid);
    if (slot < 0)
        return -1;
    irq_handlers[slot] = handler;
    return 0;
}

int irq_register_late(uint32_t intid, irq_handler handler)
{
    if (irq_register(intid, handler) < 0)
        return -1;
    int slot = irq_slot(intid);
    irq_late_eoi[slot / 32] |= 1u << (slot % 32);
    return 0;
}

void trap_set_svc_handler(void (*handler)(struct trap_frame* f, uint32_t imm))
{
    svc_handler = handler;
}

void trap_enable_tasks(void)
{
    trap_tasks = 1;
}

// Vectors.S entries 6 and 10: an FIQ is the panic stop, a
// Group 0 SGI from arch_stop_other_cpus. No frame was saved, so
// nothing returns.
void aarch64_fiq(void)
{
    uint32_t intid = (uint32_t)sysreg_read(ICC_IAR0_EL1) & 0xffffff;
    if (intid < GIC_INTID_SPURIOUS)
        sysreg_write(ICC_EOIR0_EL1, intid);
    smp_nmi_stop();
    console_printf("\nexception: FIQ (INTID %u) without a panic, halted\n", intid);
    aarch64_halt();
}

// The unexpected entries (vectors.S exception_common): no frame.
void aarch64_exception(uint64_t vector, uint64_t esr, uint64_t elr, uint64_t far, uint64_t spsr)
{
    console_printf("\nexception: %s (%s)\n", vector_names[vector & 3], vector_groups[(vector >> 2) & 3]);
    console_printf("  ESR 0x%lx (EC 0x%lx)\n  ELR 0x%lx\n  FAR 0x%lx\n  SPSR 0x%lx\n",
                   esr, esr >> 26, elr, far, spsr);
    console_printf("exception: unexpected vector %lu, halted\n", vector);
    aarch64_halt();
}

// vectors.S bad_stack: a sync exception at EL1 whose frame did not fit.
void aarch64_bad_stack(uint64_t esr, uint64_t elr, uint64_t far, uint64_t spsr, uint64_t sp)
{
    console_printf("\nexception: sync (current EL, SP_ELx) on a bad kernel stack, sp 0x%lx\n", sp);
    console_printf("  ESR 0x%lx (EC 0x%lx, %s)\n  ELR 0x%lx\n  FAR 0x%lx\n  SPSR 0x%lx\n", esr, esr >> 26,
                   ec_name((esr >> 26) & 0x3f), elr, far, spsr);
    console_printf("exception: kernel stack overflow, halted\n");
    aarch64_halt();
}
