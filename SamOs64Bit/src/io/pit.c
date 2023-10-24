// 8254 PIT: the 100 Hz scheduler tick and the stopwatch the
// TSC is calibrated against.
//
// Not in the book: it never programs the PIT, so the 32-bit kernel
// ran at the BIOS default of 18.2 Hz and the 64-bit one had no timer.

#include "pit.h"
#include "io.h"
#include "tsc.h"

#define PIT_CHANNEL0   0x40
#define PIT_CHANNEL2   0x42
#define PIT_COMMAND    0x43
#define PIT_GATE_PORT  0x61        // bit 0 gate 2, bit 1 speaker, bit 5 OUT2

// One calibration window: 1193182 / 20 input clocks = 50 ms.
#define PIT_CAL_DIVISOR  20
#define PIT_CAL_ROUNDS   3
#define PIT_CAL_MAX_POLLS 50000000u

static volatile uint64_t ticks;

// Channel 0, lobyte/hibyte, mode 2 (rate generator): one IRQ0 every
// PIT_INPUT_HZ / hz input clocks.
void pit_init(uint32_t hz){
    uint32_t divisor = (PIT_INPUT_HZ + hz / 2) / hz;
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

uint64_t pit_ticks(void){
    return ticks;
}

void pit_tick(void){
    ticks++;
}

// Channel 2 in mode 0 (interrupt on terminal count): OUT2, readable
// as bit 5 of port 0x61, goes high when the count runs out. Count the
// TSC cycles in between.
static uint64_t pit_measure_once(void){
    uint16_t latch = PIT_INPUT_HZ / PIT_CAL_DIVISOR;
    uint8_t gate = insb(PIT_GATE_PORT);
    outb(PIT_GATE_PORT, (gate & ~0x02) | 0x01);     // gate on, speaker off
    outb(PIT_COMMAND, 0xB0);                         // channel 2, lo/hi, mode 0
    outb(PIT_CHANNEL2, latch & 0xFF);
    outb(PIT_CHANNEL2, latch >> 8);                  // counting starts here
    TIME_TSC start = read_tsc();
    uint32_t polls = 0;
    while(!(insb(PIT_GATE_PORT) & 0x20)){
        if(++polls == PIT_CAL_MAX_POLLS){
            outb(PIT_GATE_PORT, gate);
            return 0;
        }
    }
    TIME_TSC end = read_tsc();
    outb(PIT_GATE_PORT, gate);
    return (end - start) * PIT_CAL_DIVISOR;
}

// Median of three windows, so one window stretched by the host
// descheduling the VM does not decide the frequency.
uint64_t pit_measure_tsc_hz(void){
    uint64_t r[PIT_CAL_ROUNDS];
    for(int i = 0; i < PIT_CAL_ROUNDS; i++){
        r[i] = pit_measure_once();
        if(!r[i]){
            return 0;
        }
    }
    for(int i = 0; i < PIT_CAL_ROUNDS; i++){
        for(int j = i + 1; j < PIT_CAL_ROUNDS; j++){
            if(r[j] < r[i]){
                uint64_t t = r[i];
                r[i] = r[j];
                r[j] = t;
            }
        }
    }
    return r[PIT_CAL_ROUNDS / 2];
}
