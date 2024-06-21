#ifndef ARCH_RISCV64_ARCH_IMPL_H
#define ARCH_RISCV64_ARCH_IMPL_H
// The inline half of arch/arch.h on riscv64. Include
// arch/arch.h, not this file.
//
// The out-of-line functions arch.h declares are in arch.c,
// paging.c, trap.c, uaccess.c and fpu.S; the riscv64 kernel is the
// shared kernel_main. The layout names of
// config.h are the probed paging mode's (layout.h).

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

// sstatus.FS, the FP unit's state: Off, Initial, Clean, Dirty.
#define SSTATUS_FS          (3ul << 13)
#define SSTATUS_FS_INITIAL  (1ul << 13)
#define SSTATUS_FS_CLEAN    (2ul << 13)

// U-mode (SPP clear) with SPIE set: interrupts open after sret.
// FP on (FS Initial), so user code may use F and D.
static inline void trap_frame_init_user(struct trap_frame* f, uint64_t entry, uint64_t sp){
    f->sepc     = entry;
    f->x[TF_SP] = sp;
    f->sstatus  = (f->sstatus & ~(SSTATUS_SPP | SSTATUS_FS)) | SSTATUS_SPIE | SSTATUS_FS_INITIAL;
}

// ------------------------------------------------------------------
// FP state: f0-f31 and fcsr (fpu.S). The kernel is rv64imac and never
// touches them, so they are saved and restored at the user boundary,
// as x86 does with fxsave. Eager: task_enter restores them
// before every return into a task; a trap from U-mode saves them only
// when sstatus.FS says Dirty, since otherwise they still hold what
// the last restore loaded.
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
// Per-CPU data: tp holds this CPU's struct cpu in the kernel. In user
// mode tp is the program's; sscratch then holds the top of this CPU's
// trap stack, and the word at that top holds the struct cpu, which
// trap_entry loads back into tp (boot.S).
// ------------------------------------------------------------------

struct arch_cpu {
    uint64_t hart_id;          // for SBI HSM hart_start and IPIs
    uint8_t* kernel_stack;     // top of the trap stack for traps from U-mode
    volatile uint32_t ipi_pending;  // 1 << enum arch_ipi, taken by the software interrupt
    int plic_ctx;              // This hart's S-mode PLIC context, -1 if none
    int imsic_hart;            // parity: this hart's index in the S-level IMSIC, -1 if none
    uint64_t next_deadline;    // This hart's next timer deadline (timer.c)
    volatile uint64_t ticks;   // Timer interrupts taken on this hart
    struct mm* mm;             // The user address space in satp, NULL for the kernel's
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
// Barriers and atomics (RVWMO). The __atomic builtins are the A
// extension (atomic_gcc.h): lr/sc with .aqrl for a compare-and-swap,
// amoswap/amoadd for exchange and add, a fence before a release store
// and after an acquire load.
// ------------------------------------------------------------------

#define barrier()   __asm__ volatile("" ::: "memory")
#define smp_mb()    __asm__ volatile("fence rw, rw" ::: "memory")
#define smp_rmb()   __asm__ volatile("fence r, r" ::: "memory")
#define smp_wmb()   __asm__ volatile("fence w, w" ::: "memory")

// Every spin loop is also where a hart stops for another's
// panic: there is no NMI, and a hart spinning with interrupts off
// would never take the stop IPI (smp.c). `pause` (Zihintpause) is
// encoded as a fence that harts without it execute as a no-op.
extern volatile uint32_t riscv64_stop_harts;
void riscv64_stop_poll(void);
#define cpu_relax() do {                                              \
        __asm__ volatile(".insn i 0x0f, 0, x0, x0, 0x010" ::: "memory"); \
        if (riscv64_stop_harts) {                                     \
            riscv64_stop_poll();                                      \
        }                                                             \
    } while (0)

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
// paging mode probed at boot (Sv48 or Sv39); the riscv64 user-mode work: so does its size
// (arch/riscv64/layout.h).
#define PAGING_PHYS_TO_VIRT(p)  phys_to_virt((uint64_t)(uintptr_t)(p))
#define PAGING_IS_DIRECT_MAP(v) ((uintptr_t)(v) >= dmap_base && \
                                 (uintptr_t)(v) <  dmap_base + dmap_size)

// mmu_memory_init direct-maps all of /memory before pfa_init,
// so the frame table may go anywhere; the e2e boot markers live in an
// array in the image (RAM does not start at 0).
#define ARCH_PFA_EARLY_MAP_LIMIT  UINT64_MAX
#define ARCH_BOOT_MARKER_SLOTS    riscv64_boot_markers
// The SMP self-test's scratch page: x86's address is the start
// of the Sv48 mmio window here (the UART), and not in the Sv39 upper
// half at all. The window's last page is never handed out.
#define ARCH_SMP_TEST_VA          mmu_scratch_va()

// ------------------------------------------------------------------
// DMA cache maintenance (arch.h, dma.c): nothing to do unless
// the device tree says dma-noncoherent (or samos.dma_noncoherent);
// then Zicbom's cbo.* when the harts have it, else a SiFive cache
// controller's Flush64 register.
// ------------------------------------------------------------------

enum { RISCV64_DMA_CLEAN, RISCV64_DMA_INVAL, RISCV64_DMA_FLUSH };
extern int riscv64_dma_mode;            // 0: coherent
void riscv64_dma_op(const volatile void* va, size_t size, int op);

static inline void arch_dma_clean(const volatile void* va, size_t size){
    if(riscv64_dma_mode){
        riscv64_dma_op(va, size, RISCV64_DMA_CLEAN);
    }
}

static inline void arch_dma_inval(const volatile void* va, size_t size){
    if(riscv64_dma_mode){
        riscv64_dma_op(va, size, RISCV64_DMA_INVAL);
    }
}

static inline void arch_dma_flush(const volatile void* va, size_t size){
    if(riscv64_dma_mode){
        riscv64_dma_op(va, size, RISCV64_DMA_FLUSH);
    }
}

#endif
