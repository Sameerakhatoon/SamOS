#ifndef ARCH_ATOMIC_GCC_H
#define ARCH_ATOMIC_GCC_H
// The atomic operations of the SMP work on the GCC __atomic
// builtins, the same on every arch: x86-64 gets lock-prefixed
// instructions, aarch64 ldaxr/stlxr loops (or LSE), riscv64 lr/sc or
// amo*. Each arch's atomic.h adds its barriers and includes this.

#include <stdint.h>
#include <stdbool.h>

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

#endif
