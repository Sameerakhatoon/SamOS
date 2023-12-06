// Trap dump, the riscv64 analogue of x86's early_fault.
// The C half of the trap path: decode scause, dispatch the
// timer and the PLIC, preempt on the way out, dump and halt on a
// kernel exception (x86's idt_exception: KERNEL FAULT plus registers).

#include <stdbool.h>
#include <stdint.h>
#include "arch/riscv64/console.h"
#include "arch/riscv64/kthread.h"
#include "arch/riscv64/plic.h"
#include "arch/riscv64/riscv64.h"
#include "arch/riscv64/timer.h"
#include "arch/riscv64/trap.h"

#define EXC_ILLEGAL_INSTRUCTION 2
#define EXC_BREAKPOINT          3

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

static void dump_frame(const char* what, const struct trap_frame* f)
{
    console_printf("\n%s: scause 0x%lx (%s)\n", what, f->scause, cause_name(f->scause));
    console_printf("  sepc 0x%lx stval 0x%lx sstatus 0x%lx\n", f->sepc, f->stval, f->sstatus);
    for (int i = 0; i < 31; i++) {
        console_printf("%s%s 0x%lx", i % 4 ? " " : "  ", reg_names[i], f->x[i]);
        if (i % 4 == 3 || i == 30) {
            console_printf("\n");
        }
    }
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

// 2 for a compressed instruction, 4 otherwise (no longer encodings
// exist yet). The MMU is off, so sepc is readable as is.
static unsigned insn_length(uint64_t pc)
{
    return (*(volatile uint16_t*)pc & 3) == 3 ? 4 : 2;
}

// The tick, x86's idt_clock: count it, then let the scheduler switch
// on the way out of the trap.
static void riscv64_clock(struct trap_frame* frame)
{
    timer_interrupt();
    sched_tick();
}

static void interrupt(struct trap_frame* f)
{
    uint64_t code = f->scause & ~SCAUSE_INTERRUPT;
    switch (code) {
    case IRQ_S_TIMER: riscv64_clock(f); break;
    case IRQ_S_EXT: plic_handle(f); break;
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

struct trap_frame* riscv64_trap(struct trap_frame* f)
{
    if (f->scause & SCAUSE_INTERRUPT) {
        interrupt(f);
        return sched_switch(f);
    }
    if (f->scause == EXC_ILLEGAL_INSTRUCTION && probe_active) {
        probe_faults++;
        f->sepc += insn_length(f->sepc);
        return f;
    }
    if (f->scause == EXC_BREAKPOINT && (f->sstatus & SSTATUS_SPP)) {
        // A kernel ebreak is a debugger stop: show it, step over it.
        dump_frame("BREAKPOINT", f);
        f->sepc += insn_length(f->sepc);
        console_printf("breakpoint: resuming at 0x%lx\n", f->sepc);
        return f;
    }
    // No user mode until the riscv64 user-mode work, where a U-mode fault kills the task
    // instead (as on x86).
    dump_frame(f->sstatus & SSTATUS_SPP ? "KERNEL FAULT" : "USER FAULT", f);
    console_printf("System halted.\n");
    riscv64_halt();
}
