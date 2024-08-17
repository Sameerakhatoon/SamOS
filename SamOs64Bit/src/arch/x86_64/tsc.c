// Lecture 128 - TSC frequency + delay.
//
// Reads the bus / core frequency through CPUID leaves 0x15 and
// 0x16 (in that order; 0x16 is a fallback), then exposes an
// RDTSC sample + a `udelay(us)` busy wait.
//
// tsc.o and tsc.asm.o (which provides `tsc_microseconds`, L129)
// are both linked into the kernel.
//
// SamOS divergence. The book:
//   - multiplied the result by 10^6 a second time, so "microseconds"
//     were seconds and udelay(us) waited about us seconds;
//   - asked leaves 0x15 and 0x16 without checking the CPUID max
//     leaf, so on a CPU without them (QEMU's Skylake-Server stops at
//     0xd) it read whatever the highest leaf returns;
//   - divided by a frequency of 0 when neither leaf had one.
// tsc_init now measures the TSC against PIT channel 2 and keeps the
// CPUID value only when it agrees with the measurement.
//
// Against the reference clock (refclock.c: HPET, ACPI PM timer
// or PIT channel 2), which needs the ACPI tables: tsc_init, early,
// only takes CPUID's value (or 1 GHz) for the time before them, and
// tsc_calibrate measures once acpi_init has run.

#include "tsc.h"
#include "cpuid.h"
#include "refclock.h"
#include "kernel.h"
#include "string/string.h"

// Used when neither CPUID nor the reference clock gives a frequency, so nothing
// ever divides by zero. Timing is then only roughly right.
#define TSC_FALLBACK_HZ  1000000000ULL

uint64_t tsc_freq_val = 0;
static TIME_TSC tsc_boot = 0;

static uint64_t tsc_cpuid_frequency(void){
    uint32_t max, eax, ebx, ecx, edx;
    uint64_t tsc_freq = 0;
    cpuid(0, 0, &max, &ebx, &ecx, &edx);

    // CPUID leaf 0x15 - TSC and bus frequency ratio.
    if(max >= 0x15){
        cpuid(0x15, 0, &eax, &ebx, &ecx, &edx);
        if(eax != 0 && ebx != 0 && ecx != 0){
            tsc_freq = ((uint64_t)ecx * (uint64_t)ebx) / (uint64_t)eax;
        }
    }

    // Fallback - CPUID leaf 0x16 base frequency (MHz).
    if(tsc_freq == 0 && max >= 0x16){
        cpuid(0x16, 0, &eax, &ebx, &ecx, &edx);
        if(eax != 0){
            tsc_freq = (uint64_t)eax * 1000000ULL;
        }
    }
    return tsc_freq;
}

static void tsc_print_khz(const char* label, uint64_t hz){
    print(label);
    print(itoa((int)(hz / 1000)));
    print(" kHz");
}

void tsc_init(void){
    uint64_t cpuid_hz = tsc_cpuid_frequency();
    tsc_freq_val = cpuid_hz ? cpuid_hz : TSC_FALLBACK_HZ;
    tsc_boot     = read_tsc();
}

// After refclock_init. Prints "tsc: cpuid N kHz, <clock> N
// kHz, using N kHz", <clock> one of hpet, pmtimer, pit or none.
void tsc_calibrate(void){
    uint64_t cpuid_hz = tsc_cpuid_frequency();
    uint64_t ref_hz   = refclock_measure_hz(read_tsc);
    uint64_t hz       = ref_hz;
    // CPUID is exact when it is right; trust it within 2 % of the
    // measurement.
    if(cpuid_hz && ref_hz &&
       cpuid_hz > ref_hz - ref_hz / 50 && cpuid_hz < ref_hz + ref_hz / 50){
        hz = cpuid_hz;
    }
    if(!hz){
        hz = cpuid_hz ? cpuid_hz : TSC_FALLBACK_HZ;
    }
    tsc_freq_val = hz;

    tsc_print_khz("tsc: cpuid ", cpuid_hz);
    print(", ");
    print(refclock_short_name());
    tsc_print_khz(" ", ref_hz);
    tsc_print_khz(", using ", hz);
    print("\n");
}

TIME_TSC tsc_frequency(void){
    if(tsc_freq_val == 0){
        tsc_init();
    }
    return (TIME_TSC)tsc_freq_val;
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

// Lecture 129 - upstream finally adds the missing parens so
// this is a real function call. The L128 cast workaround is
// removed.
TIME_SECONDS tsc_seconds(void){
    TIME_MILISECONDS miliseconds = tsc_miliseconds();
    return miliseconds / 1000;
}

// Time since tsc_init, for the uptime syscall.
TIME_MICROSECONDS tsc_uptime_microseconds(void){
    TIME_TSC hz    = tsc_frequency();
    TIME_TSC delta = read_tsc() - tsc_boot;
    return (delta / hz) * 1000000ULL + ((delta % hz) * 1000000ULL) / hz;
}

// Split so microseconds * frequency cannot overflow.
void udelay(TIME_MICROSECONDS microseconds){
    TIME_TSC tsc_freq       = tsc_frequency();
    TIME_TSC start          = read_tsc();
    TIME_TSC cycles_to_wait = (microseconds / 1000000) * tsc_freq +
                              ((microseconds % 1000000) * tsc_freq) / 1000000;
    while((read_tsc() - start) < cycles_to_wait){
        __asm__ volatile("pause");
    }
}
