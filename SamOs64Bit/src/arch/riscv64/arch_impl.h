#ifndef ARCH_RISCV64_ARCH_IMPL_H
#define ARCH_RISCV64_ARCH_IMPL_H
// The inline half of arch/arch.h on riscv64. Include
// arch/arch.h, not this file.
//
// Enough for the shared kernel code to compile here (make
// ARCH=riscv64 builds it into build/riscv64/libsamos.a). The
// out-of-line functions arch.h declares come with the riscv64 user-mode work (user mode,
// syscalls, FP save) and the riscv64 device work (devices); the trap and MMU pieces they
// build on are named next to each one in the hardware layer notes.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"
#include "arch/riscv64/trap.h"   // struct trap_frame
#include "arch/riscv64/mmu.h"    // phys_to_virt, dmap_base

#define ARCH_NAME          "riscv64"
#define ARCH_ELF_MACHINE   243     // EM_RISCV
#define ARCH_HAS_PORT_IO   0

// ------------------------------------------------------------------
// Trap frame (the riscv64 trap code's). The syscall convention is x86's mapped onto
// ecall as Linux does: a7 the number, a0-a5 the arguments, a0 the
// result (programs/stdlib/arch/riscv64/samos.S).
// ------------------------------------------------------------------

#define SSTATUS_SPP   (1ul << 8)
#define SSTATUS_SPIE  (1ul << 5)

static inline uint64_t trap_frame_pc(const struct trap_frame* f){
    return f->sepc;
}

static inline uint64_t trap_frame_sp(const struct trap_frame* f){
    return f->x[TF_SP];
}

static inline bool trap_frame_from_user(const struct trap_frame* f){
    return !(f->sstatus & SSTATUS_SPP);
}

static inline uint64_t trap_frame_syscall_nr(const struct trap_frame* f){
    return f->x[TF_A7];
}

static inline void* trap_frame_arg(const struct trap_frame* f, int n){
    return (void*)f->x[TF_A0 + (n < 5 ? n : 5)];
}

static inline void trap_frame_set_ret(struct trap_frame* f, uint64_t v){
    f->x[TF_A0] = v;
}

// sepc points at the ecall; the syscall entry steps it past (4 bytes,
// ecall has no compressed form) before the dispatch, so a restart
// steps back.
static inline void trap_frame_syscall_restart(struct trap_frame* f){
    f->sepc -= 4;
}

// U-mode (SPP clear) with SPIE set: interrupts open after sret.
static inline void trap_frame_init_user(struct trap_frame* f, uint64_t entry, uint64_t sp){
    f->sepc     = entry;
    f->x[TF_SP] = sp;
    f->sstatus  = (f->sstatus & ~SSTATUS_SPP) | SSTATUS_SPIE;
}

// ------------------------------------------------------------------
// FP state: f0-f31 and fcsr. The kernel is rv64imac (no F/D);
// Decides whether user mode gets F/D (f0-f31, or no FP)
// and saves them at the trap boundary.
// ------------------------------------------------------------------

#define ARCH_FPU_STATE_SIZE (32 * 8 + 8)

struct arch_fpu_state {
    uint8_t data[ARCH_FPU_STATE_SIZE];
} __attribute__((aligned(16)));

static inline void arch_fpu_init(struct arch_fpu_state* s){
    for(int i = 0; i < ARCH_FPU_STATE_SIZE; i++){
        s->data[i] = 0;
    }
}

void arch_fpu_save(struct arch_fpu_state* s);
void arch_fpu_restore(const struct arch_fpu_state* s);

// ------------------------------------------------------------------
// Per-CPU data: tp holds this CPU's struct cpu in the kernel
// (sscratch while in user mode).
// ------------------------------------------------------------------

struct arch_cpu {
    uint64_t hart_id;          // for SBI HSM hart_start and IPIs
    uint8_t* kernel_stack;     // the trap stack for traps from U-mode
};

static inline void* arch_cpu_self(void){
    void* c;
    __asm__ volatile("mv %0, tp" : "=r"(c));
    return c;
}

// ------------------------------------------------------------------
// Interrupt enable (sstatus.SIE)
// ------------------------------------------------------------------

#define SSTATUS_SIE   (1ul << 1)

static inline uint64_t arch_irq_save(void){
    uint64_t s;
    __asm__ volatile("csrrci %0, sstatus, 2" : "=r"(s) :: "memory");
    return s;
}

static inline void arch_irq_restore(uint64_t s){
    if(s & SSTATUS_SIE){
        __asm__ volatile("csrsi sstatus, 2" ::: "memory");
    }
}

static inline void arch_irq_enable(void){
    __asm__ volatile("csrsi sstatus, 2" ::: "memory");
}

static inline void arch_irq_disable(void){
    __asm__ volatile("csrci sstatus, 2" ::: "memory");
}

// ------------------------------------------------------------------
// Barriers and atomics (RVWMO).
// ------------------------------------------------------------------

#define barrier()   __asm__ volatile("" ::: "memory")
#define smp_mb()    __asm__ volatile("fence rw, rw" ::: "memory")
#define smp_rmb()   __asm__ volatile("fence r, r" ::: "memory")
#define smp_wmb()   __asm__ volatile("fence w, w" ::: "memory")
#define cpu_relax() barrier()          // pause needs Zihintpause

#include "arch/atomic_gcc.h"

// ------------------------------------------------------------------
// MMIO: device accesses ordered against normal memory, as Linux's
// readl/writel do (fence i,r after a read, fence w,o before a write).
// ------------------------------------------------------------------

static inline uint8_t  mmio_read8(volatile void* a)  { uint8_t  v = *(volatile uint8_t*)a;  __asm__ volatile("fence i, r" ::: "memory"); return v; }
static inline uint16_t mmio_read16(volatile void* a) { uint16_t v = *(volatile uint16_t*)a; __asm__ volatile("fence i, r" ::: "memory"); return v; }
static inline uint32_t mmio_read32(volatile void* a) { uint32_t v = *(volatile uint32_t*)a; __asm__ volatile("fence i, r" ::: "memory"); return v; }
static inline uint64_t mmio_read64(volatile void* a) { uint64_t v = *(volatile uint64_t*)a; __asm__ volatile("fence i, r" ::: "memory"); return v; }
static inline void mmio_write8(volatile void* a, uint8_t v)   { __asm__ volatile("fence w, o" ::: "memory"); *(volatile uint8_t*)a = v; }
static inline void mmio_write16(volatile void* a, uint16_t v) { __asm__ volatile("fence w, o" ::: "memory"); *(volatile uint16_t*)a = v; }
static inline void mmio_write32(volatile void* a, uint32_t v) { __asm__ volatile("fence w, o" ::: "memory"); *(volatile uint32_t*)a = v; }
static inline void mmio_write64(volatile void* a, uint64_t v) { __asm__ volatile("fence w, o" ::: "memory"); *(volatile uint64_t*)a = v; }

// ------------------------------------------------------------------
// Block fill and copy for memory.c: plain loops.
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
// Mapping flags: the portable PAGING_* bits, which the riscv64 user-mode code's paging_map
// turns into PTE bits (R/W/X/U, A/D, G).
// ------------------------------------------------------------------

#define PAGING_PAGE_SIZE        4096
#define PAGING_NO_EXECUTE       0x100
#define PAGING_CACHE_DISABLED   0b00010000
#define PAGING_WRITE_THROUGH    0b00001000
#define PAGING_ACCESS_FROM_ALL  0b00000100
#define PAGING_IS_WRITEABLE     0b00000010
#define PAGING_IS_PRESENT       0b00000001

// The direct map starts at dmap_base, which depends on the
// paging mode probed at boot (Sv48 or Sv39); the riscv64 user-mode work bounds it by mode.
#define PAGING_PHYS_TO_VIRT(p)  phys_to_virt((uint64_t)(uintptr_t)(p))
#define PAGING_IS_DIRECT_MAP(v) ((uintptr_t)(v) >= dmap_base && \
                                 (uintptr_t)(v) <  dmap_base + SAMOS_DIRECT_MAP_SIZE)

#endif
