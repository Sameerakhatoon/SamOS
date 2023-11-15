#ifndef KERNEL_PIT_H
#define KERNEL_PIT_H
// The 8254 programmable interval timer.
//
// Channel 0 drives IRQ0, the scheduler tick. Channel 2 (gated
// through port 0x61, no IRQ) is used once at boot as a stopwatch to
// measure the TSC frequency.

#include <stdint.h>

#define PIT_INPUT_HZ   1193182
#define PIT_TICK_HZ    100         // 100 Hz, a 10 ms tick

void     pit_init(uint32_t hz);
uint64_t pit_ticks(void);
void     pit_tick(void);

// TSC cycles per second measured against channel 2, or 0 when the
// channel never reached its terminal count.
uint64_t pit_measure_tsc_hz(void);
// The same for any up-counter (the LAPIC timer).
uint64_t pit_measure_hz(uint64_t (*read)(void));

#endif
