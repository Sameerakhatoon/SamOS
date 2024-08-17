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

#include <stdbool.h>

void     pit_init(uint32_t hz);
// False on a PC without an 8254 (channel 2 does not count).
bool     pit_present(void);

// TSC cycles per second measured against channel 2, or 0 when the
// channel never reached its terminal count.
uint64_t pit_measure_tsc_hz(void);
// The same for any up-counter (the LAPIC timer).
uint64_t pit_measure_hz(uint64_t (*read)(void));

#endif
