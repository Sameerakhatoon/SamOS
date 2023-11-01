// Early trap dump, the riscv64 analogue of x86's early_fault.
//
// boot.S points stvec here before riscv64_main runs, so any exception
// in early boot prints the cause and every register, then halts,
// instead of falling back into OpenSBI.

#include <stdint.h>
#include "arch/riscv64/console.h"
#include "arch/riscv64/riscv64.h"

// Saved by trap_entry in boot.S: x1..x31, then the trap CSRs.
struct early_trap_frame {
    uint64_t x[31];
    uint64_t sepc, scause, stval, sstatus;
};

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
    if (scause >> 63) {
        return "interrupt";
    }
    return scause < 16 && exceptions[scause] ? exceptions[scause] : "reserved";
}

void riscv64_early_trap(struct early_trap_frame* f)
{
    console_printf("\nEARLY FAULT: scause 0x%lx (%s)\n", f->scause, cause_name(f->scause));
    console_printf("  sepc 0x%lx stval 0x%lx sstatus 0x%lx\n", f->sepc, f->stval, f->sstatus);
    for (int i = 0; i < 31; i++) {
        console_printf("%s%s 0x%lx", i % 4 ? " " : "  ", reg_names[i], f->x[i]);
        if (i % 4 == 3 || i == 30) {
            console_printf("\n");
        }
    }
    console_printf("System halted.\n");
    riscv64_halt();
}
