// Lecture 128 - TSC frequency + delay (SOURCE-ONLY).
//
// Reads the bus / core frequency through CPUID leaves 0x15 and
// 0x16 (in that order; 0x16 is a fallback), then exposes an
// RDTSC sample + a `udelay(us)` busy wait.
//
// tsc.asm provides `tsc_microseconds` (L129); without it
// `tsc_seconds` does not link. The Makefile does NOT include
// tsc.o yet for that reason.

#include "tsc.h"
#include "cpuid.h"

uint64_t tsc_freq_val = 0;

TIME_TSC tsc_frequency(void){
    if(tsc_freq_val != 0){
        return tsc_freq_val;
    }

    uint32_t eax, ebx, ecx, edx;
    uint64_t tsc_freq = 0;

    // CPUID leaf 0x15 - TSC and bus frequency ratio.
    cpuid(0x15, 0, &eax, &ebx, &ecx, &edx);
    if(eax != 0 && ebx != 0 && ecx != 0){
        tsc_freq = ((uint64_t)ecx * (uint64_t)ebx) / (uint64_t)eax;
    }

    // Fallback - CPUID leaf 0x16 base frequency (MHz).
    if(tsc_freq == 0){
        cpuid(0x16, 0, &eax, &ebx, &ecx, &edx);
        if(eax != 0){
            tsc_freq = (uint64_t)eax * 1000000ULL;
        }
    }

    // Upstream bug preserved verbatim: the leaf-0x16 branch
    // already converts from MHz to Hz with the 1000000ULL
    // multiply. This second `tsc_freq *= 1000000` overshoots
    // the real frequency by 10^6 on every path. The leaf-0x15
    // branch produces Hz directly (`(ecx * ebx) / eax`) and
    // also gets the spurious multiply. The whole subsystem is
    // off-by-1e6 until upstream fixes it; udelay then waits
    // 10^6 times too few cycles, which on a typical 3 GHz core
    // makes `udelay(1)` return almost immediately. Documented;
    // the file is not yet linked so no observable effect.
    tsc_freq *= 1000000;
    tsc_freq_val = tsc_freq;

    return (TIME_TSC)tsc_freq;
}

TIME_TSC read_tsc(void){
    uint32_t lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi) : : "memory");
    return ((TIME_TSC)hi << 32) | lo;
}

TIME_MILISECONDS tsc_miliseconds(void){
    TIME_MICROSECONDS microseconds = tsc_microseconds();
    return microseconds / 1000;
}

// Upstream bug preserved verbatim: this function reads
// `tsc_miliseconds` without parentheses, which evaluates to a
// function pointer (decays to its address) rather than calling
// it. Dividing the address by 1000 yields garbage. Linkage
// quirk only - the file is unlinked at L128.
TIME_SECONDS tsc_seconds(void){
    TIME_MILISECONDS miliseconds = (TIME_MILISECONDS)(uintptr_t)tsc_miliseconds;
    return miliseconds / 1000;
}

void udelay(TIME_MICROSECONDS microseconds){
    TIME_TSC tsc_freq       = tsc_frequency();
    TIME_TSC start          = read_tsc();
    TIME_TSC cycles_to_wait = (microseconds * tsc_freq) / 1000000;
    while((read_tsc() - start) < cycles_to_wait){
        __asm__ volatile("pause");
    }
}
