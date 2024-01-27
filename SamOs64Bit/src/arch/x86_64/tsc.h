#ifndef KERNEL_TSC_H
#define KERNEL_TSC_H
// Lecture 128 - TSC-based delay. tsc_frequency reads CPUID
// leaves 0x15 / 0x16 for the bus frequency; read_tsc samples
// RDTSC; udelay spins until enough cycles have elapsed.
//
// SOURCE-ONLY at L128: tsc.c references tsc_microseconds
// which lives in tsc.asm (L129). The build picks up tsc.o
// once both are in.

#include <stdint.h>
#include "arch/arch.h"   // TIME_MICROSECONDS and friends

typedef uint64_t TIME_TSC;

// Measure the frequency (CPUID checked against PIT channel
// 2) and record the boot TSC. tsc_frequency calls it on first use.
void              tsc_init(void);
TIME_MICROSECONDS tsc_uptime_microseconds(void);
TIME_TSC          tsc_frequency(void);
void              udelay(TIME_MICROSECONDS microseconds);
TIME_SECONDS      tsc_seconds(void);
TIME_MILISECONDS  tsc_miliseconds(void);   // sic
TIME_TSC          read_tsc(void);

// Defined in tsc.asm (L129).
TIME_MICROSECONDS tsc_microseconds(void);

#endif
