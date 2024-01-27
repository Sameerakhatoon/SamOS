#ifndef ARCH_X86_64_ATOMIC_H
#define ARCH_X86_64_ATOMIC_H
// Atomics and memory barriers. Not in the book, which
// runs on one CPU. The GCC __atomic builtins compile to lock-prefixed
// instructions on x86-64; the barriers are spelled out so each
// architecture has its own copy of this header (included through
// arch/arch.h; the atomic operations are arch/atomic_gcc.h).
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

#include "arch/atomic_gcc.h"

#endif
