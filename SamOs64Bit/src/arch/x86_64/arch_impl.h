#ifndef ARCH_X86_64_ARCH_IMPL_H
#define ARCH_X86_64_ARCH_IMPL_H
// The inline half of arch/arch.h on x86-64. Include
// arch/arch.h, not this file.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"
#include "arch/x86_64/atomic.h"
#include "arch/x86_64/segments.h"
#include "arch/x86_64/tss.h"

#define ARCH_NAME          "x86_64"
#define ARCH_ELF_MACHINE   62      // EM_X86_64
#define ARCH_HAS_PORT_IO   1

// ------------------------------------------------------------------
// Trap frame
// ------------------------------------------------------------------

// One frame for every trap, built by idt.asm. Lowest
// address first: the 15 general registers, then the vector and error
// code the stub pushed (0 when the CPU pushes none), then what the CPU
// pushes in long mode (always ss:rsp too, even without a ring change).
// It replaces the book's struct interrupt_frame and struct registers:
// a task's saved user state is a trap_frame as well.
struct trap_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

static inline uint64_t trap_frame_pc(const struct trap_frame* f){
    return f->rip;
}

static inline uint64_t trap_frame_sp(const struct trap_frame* f){
    return f->rsp;
}

static inline bool trap_frame_from_user(const struct trap_frame* f){
    return (f->cs & 3) == 3;
}

// The syscall convention, System V style on int 0x80:
// rax = command, arguments in rdi, rsi, rdx, r10, r8, r9 (r10, not
// rcx, as in the Linux syscall ABI, so the trampolines stay the same
// for a later SYSCALL instruction), result in rax. Every other
// register is preserved. The book pushed arguments on the user stack
// and the kernel read them back with task_get_stack_item.
static inline uint64_t trap_frame_syscall_nr(const struct trap_frame* f){
    return f->rax;
}

static inline void* trap_frame_arg(const struct trap_frame* f, int n){
    switch(n){
        case 0:  return (void*)f->rdi;
        case 1:  return (void*)f->rsi;
        case 2:  return (void*)f->rdx;
        case 3:  return (void*)f->r10;
        case 4:  return (void*)f->r8;
        default: return (void*)f->r9;
    }
}

static inline void trap_frame_set_ret(struct trap_frame* f, uint64_t v){
    f->rax = v;
}

// A blocked syscall runs again when its task is woken: int 0x80 is
// CD 80, so moving rip back by its length re-executes it.
static inline void trap_frame_syscall_restart(struct trap_frame* f){
    f->rip -= 2;
}

// A new user task's first frame: ring 3 at `entry` on stack `sp`.
static inline void trap_frame_init_user(struct trap_frame* f, uint64_t entry, uint64_t sp){
    f->rip = entry;
    f->rsp = sp;
    f->cs  = USER_CODE_SEGMENT;
    f->ss  = USER_DATA_SEGMENT;
}

// ------------------------------------------------------------------
// FPU/SSE state: fxsave/fxrstor, saved at the trap
// boundary; the kernel is built -mgeneral-regs-only.
// ------------------------------------------------------------------

#define ARCH_FPU_STATE_SIZE 512

struct arch_fpu_state {
    uint8_t data[ARCH_FPU_STATE_SIZE];
} __attribute__((aligned(16)));

// Power-on state: x87 control word 0x37F and MXCSR 0x1F80 (all
// exceptions masked), registers zero.
static inline void arch_fpu_init(struct arch_fpu_state* s){
    for(int i = 0; i < ARCH_FPU_STATE_SIZE; i++){
        s->data[i] = 0;
    }
    *(uint16_t*)&s->data[0]  = 0x037F;
    *(uint32_t*)&s->data[24] = 0x1F80;
}

static inline void arch_fpu_save(struct arch_fpu_state* s){
    __asm__ volatile("fxsave64 %0" : "=m"(*s));
}

static inline void arch_fpu_restore(const struct arch_fpu_state* s){
    __asm__ volatile("fxrstor64 %0" : : "m"(*s));
}

// ------------------------------------------------------------------
// Per-CPU data
// ------------------------------------------------------------------

// The x86 part of struct cpu (cpu/percpu.h).
struct arch_cpu {
    uint8_t*   kernel_stack;    // TSS rsp0 stack, guard page at the bottom
    uint64_t   gdt[9];          // APs: own copy, TSS in slots 7 and 8
    struct tss tss;
};

// Each CPU's GS base points at its struct cpu, whose first field
// points at itself, so this is one gs-relative load.
static inline void* arch_cpu_self(void){
    void* c;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(c));
    return c;
}

// ------------------------------------------------------------------
// Interrupt flag
// ------------------------------------------------------------------

static inline uint64_t arch_irq_save(void){
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void arch_irq_restore(uint64_t flags){
    if(flags & 0x200){
        __asm__ volatile("sti" ::: "memory");
    }
}

static inline void arch_irq_enable(void){
    __asm__ volatile("sti" ::: "memory");
}

static inline void arch_irq_disable(void){
    __asm__ volatile("cli" ::: "memory");
}

// ------------------------------------------------------------------
// MMIO. x86 keeps device accesses in program order; volatile is
// enough.
// ------------------------------------------------------------------

static inline uint8_t  mmio_read8(volatile void* a)  { return *(volatile uint8_t*)a; }
static inline uint16_t mmio_read16(volatile void* a) { return *(volatile uint16_t*)a; }
static inline uint32_t mmio_read32(volatile void* a) { return *(volatile uint32_t*)a; }
static inline uint64_t mmio_read64(volatile void* a) { return *(volatile uint64_t*)a; }
static inline void mmio_write8(volatile void* a, uint8_t v)   { *(volatile uint8_t*)a = v; }
static inline void mmio_write16(volatile void* a, uint16_t v) { *(volatile uint16_t*)a = v; }
static inline void mmio_write32(volatile void* a, uint32_t v) { *(volatile uint32_t*)a = v; }
static inline void mmio_write64(volatile void* a, uint64_t v) { *(volatile uint64_t*)a = v; }

// ------------------------------------------------------------------
// Block fill and copy for memory.c: eight bytes per iteration
// (TCG runs each iteration of a rep string instruction separately),
// then the tail a byte at a time.
// ------------------------------------------------------------------

static inline void arch_memset(void* d, uint8_t c, size_t n){
    size_t   words = n / 8;
    size_t   tail  = n % 8;
    uint64_t v     = 0x0101010101010101ull * c;
    __asm__ volatile("cld; rep stosq" : "+D"(d), "+c"(words) : "a"(v) : "memory");
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(tail) : "a"(v) : "memory");
}

static inline void arch_memcpy(void* d, const void* s, size_t n){
    size_t words = n / 8;
    size_t tail  = n % 8;
    __asm__ volatile("cld; rep movsq" : "+D"(d), "+S"(s), "+c"(words) : : "memory");
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(tail) : : "memory");
}

// ------------------------------------------------------------------
// Mapping flags. The low ones are the page-table bits themselves;
// paging.c turns PAGING_NO_EXECUTE into bit 63.
// ------------------------------------------------------------------

#define PAGING_PAGE_SIZE        4096
#define PAGING_NO_EXECUTE       0x100       // NX (bit 63 of the entry)
#define PAGING_CACHE_DISABLED   0b00010000
#define PAGING_WRITE_THROUGH    0b00001000
#define PAGING_ACCESS_FROM_ALL  0b00000100  // U=1: ring 3 can see this page
#define PAGING_IS_WRITEABLE     0b00000010
#define PAGING_IS_PRESENT       0b00000001

// The direct map: physical address p is kernel virtual
// SAMOS_DIRECT_MAP_BASE + p. Every kmalloc pointer and every page
// table is a direct-map address; page-table entries hold physical ones.
static inline void* phys_to_virt(uint64_t p){
    return (void*)(uintptr_t)(p + SAMOS_DIRECT_MAP_BASE);
}
#define PAGING_PHYS_TO_VIRT(p)  phys_to_virt((uint64_t)(uintptr_t)(p))
#define PAGING_IS_DIRECT_MAP(v) ((uintptr_t)(v) >= SAMOS_DIRECT_MAP_BASE && \
                                 (uintptr_t)(v) <  SAMOS_DIRECT_MAP_BASE + SAMOS_DIRECT_MAP_SIZE)

#endif
