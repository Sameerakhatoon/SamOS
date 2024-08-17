#ifndef KERNEL_REFCLOCK_H
#define KERNEL_REFCLOCK_H
// The reference clock the TSC and the LAPIC timer are measured
// against: the HPET (ACPI HPET table), else the ACPI PM timer (FADT),
// else PIT channel 2. A PC without a PIT (most since about 2018 hide
// or drop the 8254) still has one of the first two.
//
// samos.refclock=hpet|pmtimer|pit on the command line starts the
// search at that clock (an e2e hook: it skips the ones before it).

#include <stdint.h>

enum refclock_source {
    REFCLOCK_NONE = 0,
    REFCLOCK_HPET,
    REFCLOCK_PMTIMER,
    REFCLOCK_PIT,
};

// Pick the clock and log it ("refclock: ..."). After acpi_init.
void refclock_init(void);
enum refclock_source refclock_source(void);
// "hpet", "pmtimer", "pit" or "none" (the tsc: line).
const char* refclock_short_name(void);
// "HPET", "ACPI PM timer", "PIT channel 2" or "no clock" (the
// apic: line).
const char* refclock_long_name(void);

// Counts per second of the up-counter `read` (the TSC, the LAPIC
// timer), the median of three 50 ms windows of the reference clock.
// 0 when there is no reference clock or it stopped.
uint64_t refclock_measure_hz(uint64_t (*read)(void));

#endif
