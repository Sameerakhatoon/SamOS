#ifndef ARCH_AARCH64_TRAP_H
#define ARCH_AARCH64_TRAP_H

#include <stdint.h>

// One frame for every trap vectors.S saves, the aarch64
// counterpart of x86's struct trap_frame: lowest address
// first, the 31 general registers, then the interrupted SP and the
// EL1 exception registers. A kernel thread's saved state is the
// trap_frame its last IRQ left on its own stack.
//
// Restoring an EL1 frame puts SP back to just above the frame, so
// for those `sp` is informational: a handler may change x0-x30, elr
// and spsr, not sp (trap.c checks). For frames from EL0, sp is
// the user's SP_EL0, restored with the frame.
struct trap_frame {
    uint64_t x[31];     // x0-x30 (x29 frame pointer, x30 link register)
    uint64_t sp;        // SP at the trap
    uint64_t elr;       // ELR_EL1: where eret resumes
    uint64_t spsr;      // SPSR_EL1: the PSTATE eret restores
    uint64_t esr;       // ESR_EL1: syndrome (synchronous exceptions)
    uint64_t far;       // FAR_EL1: faulting address (aborts)
};

#define TRAP_FRAME_SIZE 288     // vectors.S hard-codes the offsets
_Static_assert(sizeof(struct trap_frame) == TRAP_FRAME_SIZE, "vectors.S frame layout");

#define SPSR_EL1H       0x5     // M[3:0]: EL1 with SP_EL1
#define SPSR_DAF        0x340   // D, A, F masked, I (IRQ) open

// Vector table entries (VBAR_EL1 + 0x80 * n): four groups of sync,
// IRQ, FIQ, SError. The EL1h and EL0 sync and IRQ entries
// build a frame; the rest are unexpected and dump and halt.
#define VECTOR_EL1H_SYNC 4
#define VECTOR_EL1H_IRQ  5
#define VECTOR_EL0_SYNC  8
#define VECTOR_EL0_IRQ   9

// From vectors.S: returns the frame to resume, `f` itself or another
// thread's after a switch.
struct trap_frame* aarch64_trap(struct trap_frame* f, uint64_t vector);

// IRQ handler for an INTID. Before trap_enable_tasks it runs between
// acknowledge and EOI; after, the EOI comes first (trap.c).
typedef void (*irq_handler)(struct trap_frame* f);
int  irq_register(uint32_t intid, irq_handler handler);
// A device completion line: after trap_enable_tasks its EOI
// follows the handler (the line must be quiet first), so the handler
// has to return.
int  irq_register_late(uint32_t intid, irq_handler handler);

// SVC from EL1: the test hook's handler.
void trap_set_svc_handler(void (*handler)(struct trap_frame* f, uint32_t imm));
// Traps go to the shared kernel from now on (arch_trap_init):
// syscalls, user faults, task state saved around IRQs from EL0.
void trap_enable_tasks(void);

// Dump the frame with the ESR decoded, then halt.
void trap_dump(const struct trap_frame* f, uint64_t vector);
void aarch64_halt(void) __attribute__((noreturn));

static inline uint64_t irq_save(void)
{
    uint64_t daif;
    __asm__ volatile("mrs %0, daif\n\tmsr daifset, #2" : "=r"(daif) :: "memory");
    return daif;
}

static inline void irq_restore(uint64_t daif)
{
    __asm__ volatile("msr daif, %0" :: "r"(daif) : "memory");
}

static inline void irq_enable(void)
{
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

#endif
