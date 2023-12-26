#ifndef SAMOS_RISCV64_H
#define SAMOS_RISCV64_H

// riscv64 entry points shared by the files in this directory.
// CSR access and the sstatus/sie bits the trap path uses.

#include <stdbool.h>
#include <stdint.h>

struct fdt;

// samos.test on the command line: halting powers QEMU off instead.
extern bool riscv64_test_mode;

void riscv64_main(uint64_t hartid, uint64_t dtb);
__attribute__((noreturn)) void riscv64_halt(void);
// The /cpus child whose reg is `hartid`, or a negative FDT_ERR_*.
int riscv64_cpu_node(const struct fdt* f, uint64_t hartid);

#define csr_read(csr) ({ uint64_t v_; __asm__ volatile("csrr %0, " #csr : "=r"(v_)); v_; })
#define csr_write(csr, v) __asm__ volatile("csrw " #csr ", %0" : : "rK"((uint64_t)(v)) : "memory")
#define csr_set(csr, v) __asm__ volatile("csrs " #csr ", %0" : : "rK"((uint64_t)(v)) : "memory")
#define csr_clear(csr, v) __asm__ volatile("csrc " #csr ", %0" : : "rK"((uint64_t)(v)) : "memory")
#define csr_read_clear(csr, v) ({ uint64_t v_; \
    __asm__ volatile("csrrc %0, " #csr ", %1" : "=r"(v_) : "rK"((uint64_t)(v)) : "memory"); v_; })

#define SSTATUS_SIE  (1ul << 1)
#define SSTATUS_SPIE (1ul << 5)
#define SSTATUS_SPP  (1ul << 8)   // previous mode: 1 = S, 0 = U
#define SSTATUS_SUM  (1ul << 18)  // S-mode may access U pages

// sie / sip bits, and the interrupt causes (scause with bit 63 set).
#define IRQ_S_SOFT   1
#define IRQ_S_TIMER  5
#define IRQ_S_EXT    9
#define SIE_SSIE     (1ul << IRQ_S_SOFT)
#define SIE_STIE     (1ul << IRQ_S_TIMER)
#define SIE_SEIE     (1ul << IRQ_S_EXT)
#define SCAUSE_INTERRUPT (1ul << 63)

// The time CSR (Zicntr) by number: the assembler only knows the name
// with _zicntr in -march.
static inline uint64_t rdtime(void)
{
    return csr_read(0xc01);
}

// Interrupts off around a critical section; restore puts SIE back.
static inline uint64_t irq_save(void)
{
    return csr_read_clear(sstatus, SSTATUS_SIE) & SSTATUS_SIE;
}

static inline void irq_restore(uint64_t flags)
{
    csr_set(sstatus, flags);
}

static inline void wfi(void)
{
    __asm__ volatile("wfi" : : : "memory");
}

#endif
