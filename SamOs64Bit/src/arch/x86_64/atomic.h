#ifndef ARCH_X86_64_ATOMIC_H
#define ARCH_X86_64_ATOMIC_H
// Atomics and memory barriers. Not in the book, which
// runs on one CPU. The GCC __atomic builtins compile to lock-prefixed
// instructions on x86-64; the barriers are spelled out so the hardware layer can
// give each architecture its own copy of this header.
//
// x86 keeps loads and stores in order except a store followed by a
// load from another address, so smp_mb is the only fence that costs
// anything; smp_rmb and smp_wmb only stop the compiler.

#include <stdint.h>
#include <stdbool.h>

#define barrier()   __asm__ volatile("" ::: "memory")
#define smp_mb()    __asm__ volatile("mfence" ::: "memory")
#define smp_rmb()   barrier()
#define smp_wmb()   barrier()
#define cpu_relax() __asm__ volatile("pause" ::: "memory")

static inline uint32_t atomic_load(volatile uint32_t* p){
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static inline void atomic_store(volatile uint32_t* p, uint32_t v){
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

// Return the new value.
static inline uint32_t atomic_add(volatile uint32_t* p, uint32_t v){
    return __atomic_add_fetch(p, v, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_inc(volatile uint32_t* p){
    return atomic_add(p, 1);
}

static inline uint32_t atomic_dec(volatile uint32_t* p){
    return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_inc64(volatile uint64_t* p){
    return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_xchg(volatile uint32_t* p, uint32_t v){
    return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
}

// Compare-and-swap: if *p == expected, store desired and return
// true; otherwise leave *p alone and return false.
static inline bool atomic_cas(volatile uint32_t* p, uint32_t expected, uint32_t desired){
    return __atomic_compare_exchange_n(p, &expected, desired, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline bool atomic_cas64(volatile uint64_t* p, uint64_t expected, uint64_t desired){
    return __atomic_compare_exchange_n(p, &expected, desired, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

// RFLAGS.IF save and restore around a critical section.
static inline uint64_t irq_save(void){
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags){
    if(flags & 0x200){
        __asm__ volatile("sti" ::: "memory");
    }
}

#endif
