#ifndef ARCH_AARCH64_ARCH_IMPL_H
#define ARCH_AARCH64_ARCH_IMPL_H
// The inline half of arch/arch.h on aarch64. Include
// arch/arch.h, not this file.
//
// The out-of-line functions arch.h declares are in arch.c
// (boot, interrupts, timer, firmware, power), paging.c (MMU),
// uaccess.c (task/uaccess.h), entry.S (return to EL0, FP/SIMD) and
// trap.c; device interrupt lines come with the aarch64 device work, other CPUs with the aarch64 SMP work.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"
#include "arch/aarch64/trap.h"   // struct trap_frame
#include "arch/aarch64/mmu.h"    // phys_to_virt, virt_to_phys

#define ARCH_NAME          "aarch64"
#define ARCH_ELF_MACHINE   183     // EM_AARCH64
#define ARCH_HAS_PORT_IO   0

// ------------------------------------------------------------------
// Trap frame (the aarch64 trap code's, vectors.S). The syscall convention is x86's
// mapped onto svc as Linux does: x8 the number, x0-x5 the arguments,
// x0 the result (programs/stdlib/arch/aarch64/samos.S).
// ------------------------------------------------------------------

#define SPSR_MODE_MASK  0xF
#define SPSR_EL0T       0x0

static inline uint64_t trap_frame_pc(const struct trap_frame* f){
    return f->elr;
}

static inline uint64_t trap_frame_sp(const struct trap_frame* f){
    return f->sp;
}

static inline bool trap_frame_from_user(const struct trap_frame* f){
    return (f->spsr & SPSR_MODE_MASK) == SPSR_EL0T;
}

static inline uint64_t trap_frame_syscall_nr(const struct trap_frame* f){
    return f->x[8];
}

static inline void* trap_frame_arg(const struct trap_frame* f, int n){
    return (void*)f->x[n < 5 ? n : 5];
}

static inline void trap_frame_set_ret(struct trap_frame* f, uint64_t v){
    f->x[0] = v;
}

// ELR_EL1 of an svc is the next instruction; svc is 4 bytes.
static inline void trap_frame_syscall_restart(struct trap_frame* f){
    f->elr -= 4;
}

// EL0t with all of DAIF clear: interrupts open in user mode.
static inline void trap_frame_init_user(struct trap_frame* f, uint64_t entry, uint64_t sp){
    f->elr  = entry;
    f->sp   = sp;
    f->spsr = SPSR_EL0T;
}

// ------------------------------------------------------------------
// FP/SIMD state: q0-q31, then FPSR and FPCR (8 bytes each). The
// kernel is built -mgeneral-regs-only, so the user's values are still
// in the registers at the trap boundary, where task.c saves and
// restores them (entry.S).
// ------------------------------------------------------------------

#define ARCH_FPU_STATE_SIZE (32 * 16 + 16)

struct arch_fpu_state {
    uint8_t data[ARCH_FPU_STATE_SIZE];
} __attribute__((aligned(16)));

// Reset state: registers zero, FPCR 0 (round to nearest, no traps).
static inline void arch_fpu_init(struct arch_fpu_state* s){
    for(int i = 0; i < ARCH_FPU_STATE_SIZE; i++){
        s->data[i] = 0;
    }
}

void arch_fpu_save(struct arch_fpu_state* s);
void arch_fpu_restore(const struct arch_fpu_state* s);

// ------------------------------------------------------------------
// Per-CPU data: TPIDR_EL1 holds this CPU's struct cpu.
// ------------------------------------------------------------------

// Vectors.S reads the first two fields through TPIDR_EL1 at
// fixed offsets (CPU_STACK_BOTTOM there, checked in arch.c).
struct arch_cpu {
    uint8_t* stack_bottom;     // lowest address of this CPU's kernel stack
    uint8_t* kernel_stack;     // top of the stack traps from EL0 start on
    uint64_t mpidr;            // MPIDR_EL1 affinity, for PSCI CPU_ON
    uint64_t gicr;             // its GIC redistributor (RD_base, virtual; gicv3.c)
};

// TLB maintenance is broadcast (tlbi ...is, paging.c): a changed
// mapping needs no shootdown IPI (cpu/smp.c).
#define ARCH_TLB_BROADCAST 1

static inline void* arch_cpu_self(void){
    void* c;
    __asm__ volatile("mrs %0, tpidr_el1" : "=r"(c));
    return c;
}

// ------------------------------------------------------------------
// Interrupt mask (DAIF.I)
// ------------------------------------------------------------------

#define DAIF_I 0x80

static inline uint64_t arch_irq_save(void){
    uint64_t daif;
    __asm__ volatile("mrs %0, daif\n\tmsr daifset, #2" : "=r"(daif) :: "memory");
    return daif;
}

static inline void arch_irq_restore(uint64_t daif){
    if(!(daif & DAIF_I)){
        __asm__ volatile("msr daifclr, #2" ::: "memory");
    }
}

static inline void arch_irq_enable(void){
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

static inline void arch_irq_disable(void){
    __asm__ volatile("msr daifset, #2" ::: "memory");
}

// ------------------------------------------------------------------
// Barriers and atomics. Inner shareable is every CPU of the system.
// ------------------------------------------------------------------

#define barrier()   __asm__ volatile("" ::: "memory")
#define smp_mb()    __asm__ volatile("dmb ish" ::: "memory")
#define smp_rmb()   __asm__ volatile("dmb ishld" ::: "memory")
#define smp_wmb()   __asm__ volatile("dmb ishst" ::: "memory")
#define cpu_relax() __asm__ volatile("yield" ::: "memory")

#include "arch/atomic_gcc.h"

// ------------------------------------------------------------------
// MMIO: Device-nGnRE memory keeps accesses to one device in order;
// the barriers order them against normal memory (DMA descriptors), as
// Linux's readl/writel do.
// ------------------------------------------------------------------

static inline uint8_t  mmio_read8(volatile void* a)  { uint8_t  v = *(volatile uint8_t*)a;  __asm__ volatile("dmb oshld" ::: "memory"); return v; }
static inline uint16_t mmio_read16(volatile void* a) { uint16_t v = *(volatile uint16_t*)a; __asm__ volatile("dmb oshld" ::: "memory"); return v; }
static inline uint32_t mmio_read32(volatile void* a) { uint32_t v = *(volatile uint32_t*)a; __asm__ volatile("dmb oshld" ::: "memory"); return v; }
static inline uint64_t mmio_read64(volatile void* a) { uint64_t v = *(volatile uint64_t*)a; __asm__ volatile("dmb oshld" ::: "memory"); return v; }
static inline void mmio_write8(volatile void* a, uint8_t v)   { __asm__ volatile("dmb oshst" ::: "memory"); *(volatile uint8_t*)a = v; }
static inline void mmio_write16(volatile void* a, uint16_t v) { __asm__ volatile("dmb oshst" ::: "memory"); *(volatile uint16_t*)a = v; }
static inline void mmio_write32(volatile void* a, uint32_t v) { __asm__ volatile("dmb oshst" ::: "memory"); *(volatile uint32_t*)a = v; }
static inline void mmio_write64(volatile void* a, uint64_t v) { __asm__ volatile("dmb oshst" ::: "memory"); *(volatile uint64_t*)a = v; }

// ------------------------------------------------------------------
// Block fill and copy for memory.c: plain loops (-mstrict-align: no
// unaligned wide accesses).
// ------------------------------------------------------------------

static inline void arch_memset(void* d, uint8_t c, size_t n){
    uint8_t* p = d;
    while(n--){
        *p++ = c;
    }
}

static inline void arch_memcpy(void* d, const void* s, size_t n){
    uint8_t* dp = d;
    const uint8_t* sp = s;
    while(n--){
        *dp++ = *sp++;
    }
}

// ------------------------------------------------------------------
// Mapping flags: the portable PAGING_* bits, which the aarch64 user-mode code's paging_map
// turns into descriptor bits (AP[2:1], PXN/UXN, AttrIndx, nG).
// ------------------------------------------------------------------

#define PAGING_PAGE_SIZE        4096
#define PAGING_NO_EXECUTE       0x100
#define PAGING_CACHE_DISABLED   0b00010000
#define PAGING_WRITE_THROUGH    0b00001000
#define PAGING_ACCESS_FROM_ALL  0b00000100
#define PAGING_IS_WRITEABLE     0b00000010
#define PAGING_IS_PRESENT       0b00000001

// The direct map at DIRECT_MAP_BASE (mmu.h), the same base as
// x86-64's SAMOS_DIRECT_MAP_BASE.
#define PAGING_PHYS_TO_VIRT(p)  phys_to_virt((uint64_t)(uintptr_t)(p))
#define PAGING_IS_DIRECT_MAP(v) ((uintptr_t)(v) >= SAMOS_DIRECT_MAP_BASE && \
                                 (uintptr_t)(v) <  SAMOS_DIRECT_MAP_BASE + SAMOS_DIRECT_MAP_SIZE)

// mmu_early's direct map covers all RAM before kernel_main, so
// the shared PFA may put its frame table anywhere (memory/pfa.c).
#define ARCH_PFA_EARLY_MAP_LIMIT  (~0ULL)

// RAM does not start at 0 here, so the boot markers
// (bootmarker.h) are a page of .bss (arch.c) instead of physical 0x6000.
extern uint64_t aarch64_boot_markers[];
#define ARCH_BOOT_MARKER_SLOTS    ((volatile uint64_t*)aarch64_boot_markers)

// DMA cache maintenance (arch.h): coherent on QEMU's virt. A
// board whose devices do not snoop fills these in with dc cvac,
// dc ivac and dc civac.
static inline void arch_dma_clean(const volatile void* va, size_t size){ }
static inline void arch_dma_inval(const volatile void* va, size_t size){ }
static inline void arch_dma_flush(const volatile void* va, size_t size){ }

#endif
