// Lecture 126 - CPUID intrinsic wrapper.
//
// Single instruction with explicit GCC asm constraints:
//   inputs : leaf -> eax, subleaf -> ecx
//   outputs: eax/ebx/ecx/edx
// Clobbers no other registers; cpuid is a serialising
// instruction so volatile is required to keep the optimiser
// from hoisting it.

#include "cpuid.h"

void cpuid(uint32_t leaf, uint32_t subleaf,
           uint32_t* eax, uint32_t* ebx,
           uint32_t* ecx, uint32_t* edx){
    asm volatile("cpuid"
                 : "=a"(*eax),
                   "=b"(*ebx),
                   "=c"(*ecx),
                   "=d"(*edx)
                 : "a"(leaf), "c"(subleaf));
}
