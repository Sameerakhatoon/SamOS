#ifndef KERNEL_CPUID_H
#define KERNEL_CPUID_H
// Lecture 126 - x86 CPUID wrapper. Used by the L128+ TSC delay
// path to read the frequency / brand / hypervisor leaves.

#include <stdint.h>

void cpuid(uint32_t leaf, uint32_t subleaf,
           uint32_t* eax, uint32_t* ebx,
           uint32_t* ecx, uint32_t* edx);

#endif
