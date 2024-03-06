#ifndef ARCH_AARCH64_TIMER_H
#define ARCH_AARCH64_TIMER_H

#include <stdint.h>
#include "firmware/fdt.h"

struct trap_frame;

// ARM generic timer: the system counter (CNTPCT_EL0) as the
// clock and the EL1 physical timer as the scheduler tick, the aarch64
// counterpart of the x86 TSC and LAPIC timer (io/tsc.h, io/apic.h).

// Counter frequency from CNTFRQ_EL0, checked against the DT's
// arm,armv8-timer clock-frequency; INTID from the DT interrupts (the
// non-secure EL1 physical timer, PPI 14 = INTID 30 on QEMU virt).
// 0, or -1 (printed) with no usable frequency.
int      timer_init(const struct fdt* f);

// Tick at `hz`, calling on_tick with the IRQ's frame (NULL for none;
// task_clock once kernel_main runs, arch_timer_start). The
// compare value advances by exactly one period per tick, so ticks
// track the counter without drift; a late IRQ is followed at once by
// the next one.
void     timer_start(uint32_t hz, void (*on_tick)(struct trap_frame* f));
uint64_t timer_ticks(void);
uint32_t timer_intid(void);

uint64_t counter_read(void);
uint64_t counter_hz(void);

#endif
