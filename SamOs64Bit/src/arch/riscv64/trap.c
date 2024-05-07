// Trap dump, the riscv64 analogue of x86's early_fault.
// The C half of the trap path: decode scause, dispatch the
// timer and the PLIC, preempt on the way out, dump and halt on a
// kernel exception (as on x86: KERNEL FAULT plus registers).
// Traps from U-mode go to the shared kernel as x86's do
// (interrupt_handler, isr80h_handler): an ecall is a syscall, an
// interrupt saves the task's state first, a fault kills the process.

#include <stdbool.h>
#include <stdint.h>
#include "arch/arch.h"
#include "arch/riscv64/console.h"
#include "arch/riscv64/kthread.h"
#include "arch/riscv64/plic.h"
#include "arch/riscv64/riscv64.h"
#include "arch/riscv64/timer.h"
#include "arch/riscv64/trap.h"
#include "cpu/percpu.h"
#include "cpu/smp.h"
#include "isr80h/isr80h.h"
#include "kernel.h"
#include "task/process.h"
#include "task/task.h"

#define EXC_ILLEGAL_INSTRUCTION 2
#define EXC_BREAKPOINT          3
#define EXC_ECALL_U             8

bool riscv64_bringup;

static const char* const reg_names[31] = {
    "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1",
    "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6",
    "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6",
};

static const char* cause_name(uint64_t scause)
{
    static const char* const exceptions[16] = {
        "instruction address misaligned", "instruction access fault",
        "illegal instruction", "breakpoint", "load address misaligned",
        "load access fault", "store address misaligned", "store access fault",
        "ecall from U-mode", "ecall from S-mode", 0, 0,
        "instruction page fault", "load page fault", 0, "store page fault",
    };
    if (scause & SCAUSE_INTERRUPT) {
        uint64_t code = scause & ~SCAUSE_INTERRUPT;
        return code == IRQ_S_SOFT ? "software interrupt" : code == IRQ_S_TIMER ? "timer interrupt"
             : code == IRQ_S_EXT ? "external interrupt" : "interrupt";
    }
    return scause < 16 && exceptions[scause] ? exceptions[scause] : "reserved";
}

void arch_dump_frame(struct trap_frame* f)
{
    console_printf("  sepc 0x%lx stval 0x%lx sstatus 0x%lx\n", f->sepc, f->stval, f->sstatus);
    for (int i = 0; i < 31; i++) {
        console_printf("%s%s 0x%lx", i % 4 ? " " : "  ", reg_names[i], f->x[i]);
        if (i % 4 == 3 || i == 30) {
            console_printf("\n");
        }
    }
}

static void dump_frame(const char* what, struct trap_frame* f)
{
    console_printf("\n%s: scause 0x%lx (%s)\n", what, f->scause, cause_name(f->scause));
    arch_dump_frame(f);
}

static volatile bool probe_active;
static volatile unsigned probe_faults;

void trap_probe_begin(void)
{
    probe_faults = 0;
    probe_active = true;
}

unsigned trap_probe_end(void)
{
    probe_active = false;
    return probe_faults;
}

// __ex_table (linker.ld): instruction, fixup.
struct ex_entry {
    uint64_t insn, fixup;
};
extern const struct ex_entry __ex_table_start[], __ex_table_end[];
static uint64_t fixups, fixup_cause;

uint64_t trap_fixup_count(void)
{
    return fixups;
}

uint64_t trap_fixup_last_cause(void)
{
    return fixup_cause;
}

// Access faults (5, 7) and page faults (12, 13, 15) only: anything
// else at a listed instruction is still a bug.
static bool fixup(struct trap_frame* f)
{
    uint64_t c = f->scause;
    if (!(f->sstatus & SSTATUS_SPP) || !(c == 5 || c == 7 || c == 12 || c == 13 || c == 15)) {
        return false;
    }
    for (const struct ex_entry* e = __ex_table_start; e < __ex_table_end; e++) {
        if (e->insn == f->sepc) {
            fixups++;
            fixup_cause = c;
            f->sepc = e->fixup;
            return true;
        }
    }
    return false;
}

// 2 for a compressed instruction, 4 otherwise (no longer encodings
// exist yet). sepc is kernel text, mapped readable.
static unsigned insn_length(uint64_t pc)
{
    return (*(volatile uint16_t*)pc & 3) == 3 ? 4 : 2;
}

// The tick, as x86's clock handler: count it and program the next
// deadline, then the scheduler: the riscv64 trap code's kthreads switch on the way out
// of the trap (samos.bringup), the shared one (task_clock) inside it.
static void riscv64_clock(struct trap_frame* frame)
{
    timer_interrupt();
    if (riscv64_bringup) {
        sched_tick();
    } else {
        task_clock(frame);
    }
}

// An IPI is sip.SSIP (set by this hart or by the SBI for
// another), with the kinds in this hart's ipi_pending (arch_send_ipi).
// Clear the bit first: one sent while the handlers run comes back.
static void software_interrupt(struct trap_frame* f)
{
    csr_clear(sip, SIE_SSIE);
    uint32_t pending = __atomic_exchange_n(&this_cpu()->arch.ipi_pending, 0, __ATOMIC_ACQ_REL);
    for (int ipi = 0; pending; ipi++, pending >>= 1) {
        if (pending & 1) {
            smp_handle_ipi((enum arch_ipi)ipi, f);
        }
    }
}

static void interrupt(struct trap_frame* f)
{
    uint64_t code = f->scause & ~SCAUSE_INTERRUPT;
    switch (code) {
    case IRQ_S_TIMER: riscv64_clock(f); break;
    case IRQ_S_EXT: plic_handle(f); break;
    case IRQ_S_SOFT: software_interrupt(f); break;
    default:
        // Nothing raises the others yet; mask one so it cannot storm.
        console_printf("trap: unexpected %s (scause 0x%lx), masked\n", cause_name(f->scause), f->scause);
        if (code < 64) {
            csr_clear(sie, 1ul << code);
            csr_clear(sip, 1ul << code);
        }
        break;
    }
}

// The x86 vector of the same kind of fault, so the parent's
// status is the same on every arch (as aarch64 does): page
// faults 14, illegal instruction 6 (#UD), misaligned 17 (#AC),
// breakpoint 3, anything else 13 (#GP).
static int fault_vector(uint64_t scause)
{
    switch (scause) {
    case 12: case 13: case 15: return 14;
    case 2: return 6;
    case 0: case 4: case 6: return 17;
    case 3: return 3;
    default: return 13;
    }
}

// As on x86, a fault in U-mode kills only that process. The
// parent's wait sees 128 + the x86 vector (fault_vector).
static void user_fault(struct trap_frame* f)
{
    kernel_page();
    struct process* process = task_current()->process;
    console_printf("\nUSER FAULT: scause 0x%lx (%s) in %s: sepc 0x%lx stval 0x%lx\nprocess killed\n",
                   f->scause, cause_name(f->scause), process->filename, f->sepc, f->stval);
    process_exit(process, PROCESS_EXIT_FAULT_BASE + fault_vector(f->scause));
    task_next();
}

// A trap from U-mode, on this hart's trap stack. Anything that
// switches tasks (task_clock, a blocking syscall, an exit) does it in
// task_next and never comes back here; otherwise the task resumes from
// this frame.
static struct trap_frame* user_trap(struct trap_frame* f)
{
    if (f->scause == EXC_ECALL_U) {
        // ecall has no compressed form; the restart of a blocked
        // syscall steps back the same 4 bytes.
        f->sepc += 4;
        isr80h_handler(f);
        return f;
    }
    if (!(f->scause & SCAUSE_INTERRUPT)) {
        user_fault(f);
    }
    kernel_page();
    task_current_save_state(f);
    interrupt(f);
    // A kill from another CPU takes effect here.
    task_check_killed();
    task_page();
    return f;
}

static uint64_t sum_in_frame, sum_in_handler;

void trap_sum_counts(uint64_t* in_frame, uint64_t* in_handler)
{
    *in_frame = sum_in_frame;
    *in_handler = sum_in_handler;
}

struct trap_frame* riscv64_trap(struct trap_frame* f)
{
    sum_in_frame += (f->sstatus & SSTATUS_SUM) != 0;
    sum_in_handler += (csr_read(sstatus) & SSTATUS_SUM) != 0;
    if (!(f->sstatus & SSTATUS_SPP) && !riscv64_bringup) {
        return user_trap(f);
    }
    if (f->scause & SCAUSE_INTERRUPT) {
        interrupt(f);
        return riscv64_bringup ? sched_switch(f) : f;
    }
    if (f->scause == EXC_ILLEGAL_INSTRUCTION && probe_active) {
        probe_faults++;
        f->sepc += insn_length(f->sepc);
        return f;
    }
    if (fixup(f)) {
        return f;
    }
    if (f->scause == EXC_BREAKPOINT && (f->sstatus & SSTATUS_SPP)) {
        // A kernel ebreak is a debugger stop: show it, step over it.
        dump_frame("BREAKPOINT", f);
        f->sepc += insn_length(f->sepc);
        console_printf("breakpoint: resuming at 0x%lx\n", f->sepc);
        return f;
    }
    dump_frame(f->sstatus & SSTATUS_SPP ? "KERNEL FAULT" : "USER FAULT", f);
    console_printf("System halted.\n");
    riscv64_halt();
}
